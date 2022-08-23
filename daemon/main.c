/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_config.h"
#include "partaked_daemon.h"

#include <dropt.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/un.h> // For struct sockaddr_un
#endif

/*
 * The prefix myopt_ is used in this file for local extensions to dropt.
 * All such symbols follow dropt's support for wide char handling.
 *
 * We do not bother to use partaked_malloc() for option parsing (dropt doesn't
 * support replacing malloc()).
 */

// Parse an option argument with optional K/M/G suffix (case sensitive)
static dropt_error myopt_handle_size(dropt_context *context,
                                     const dropt_option *option,
                                     const dropt_char *arg, void *dest) {
    size_t *out = dest;

    if (out == NULL) {
        DROPT_MISUSE("No handler destination specified");
        return dropt_error_bad_configuration;
    }

    if (arg == NULL || arg[0] == DROPT_TEXT_LITERAL('\0')) {
        return dropt_error_insufficient_arguments;
    }

    dropt_char *end;
    errno = 0;
    long long n = strtoll(arg, &end, 10);
    if (end == arg || n < 0) {
        return dropt_error_mismatch;
    }
    if (errno == ERANGE) {
        return dropt_error_overflow;
    } else if (errno != 0) {
        return dropt_error_unknown;
    }

    long long f = 1;
    switch (*end) {
    case DROPT_TEXT_LITERAL('\0'):
        break;
    case DROPT_TEXT_LITERAL('B'):
    case DROPT_TEXT_LITERAL('b'):
        ++end;
        break;
    case DROPT_TEXT_LITERAL('K'):
    case DROPT_TEXT_LITERAL('k'):
        f = 1 << 10;
        ++end;
        break;
    case DROPT_TEXT_LITERAL('M'):
    case DROPT_TEXT_LITERAL('m'):
        f = 1 << 20;
        ++end;
        break;
    case DROPT_TEXT_LITERAL('G'):
    case DROPT_TEXT_LITERAL('g'):
        f = 1 << 30;
        ++end;
        break;
    default:
        return dropt_error_mismatch;
    }
    if (*end != DROPT_TEXT_LITERAL('\0')) {
        return dropt_error_mismatch;
    }

    long long val = n * f; // f != 0
    if (val / f != n || val > SIZE_MAX) {
        return dropt_error_overflow;
    }

    *out = (size_t)val;
    return dropt_error_none;
}

struct parsed_options {
    size_t memory;
    dropt_char *socket;
    dropt_char *raw_socket;
    dropt_bool force;
    dropt_char *name;
    dropt_char *file;
    dropt_bool posix;
    dropt_bool systemv;
    dropt_bool windows;
    dropt_bool huge_pages;
    dropt_bool large_pages;
    dropt_bool help;
    dropt_bool version;
};

static void print_help_prolog(FILE *file) {
    fputs("Usage: partaked -m <size> -s <name> [advanced options]\n", file);
    fputs("\n", file);
    fputs("Options:\n", file);
}

static void print_help_epilog(FILE *file) {
    fputs( // clang-format off
"Client connection:\n"
"  Either --socket or --socket-fullname must be passed. Normally,\n"
"  --socket is more convenient; it must be a string of length 1 to 80\n"
"  containing no slashes or backslashes. On Unix-like systems,\n"
"  the pathname for a Unix domain socket is derived by prepending\n"
"  (usually) \"/tmp/\" to the name. On Windows, the name of a named\n"
"  pipe is derived by prepending \"\\\\.\\pipe\\\" to the name.\n"
"  The --socket-fullname option allows full control over the socket or\n"
"  named pipe name to be used, allowing sockets at arbitrary paths on\n"
"  Unix and longer names on Windows.\n"
"  In either case, the same socket name must be passed to clients so\n"
"  that they can connect.\n"
"\n"
"Unix shared memory:\n"
"  [--posix] [--name=/myshmem]: Create with shm_open(2) and map with\n"
"      mmap(2). If name is given it should start with a slash and\n"
"      contain no more slashes.\n"
"  --systemv [--name=key]: Create with shmget(2) and map with shmat(2).\n"
"      If name is given it must be an integer key.\n"
"  --file=myfile: Create with open(2) and map with mmap(2). The --name\n"
"      option is ignored.\n"
"  Not all of the above may be available on a given Unix-like system.\n"
"  On Linux, huge pages can be allocated either by using --file with a\n"
"  location in a mounted hugetlbfs or by giving --huge-pages with\n"
"  --systemv. In both cases, --memory must be a multiple of the huge\n"
"  page size.\n"
"\n"
"Windows shared memory:\n"
"  [--windows] [--name=Local\\myshmem]: A named file mapping backed by\n"
"      the system paging file is created. If name is given it should\n"
"      start with \"Local\\\" and contain no further backslashes.\n"
"  --file=myfile [--name=Local\\myshmem]: A named file mapping backed\n"
"      by the given file is created. Usage of --name is the same as\n"
"      with --windows.\n"
"  On Windows, --large-pages can be specified with --windows (but not\n"
"  --file). This requires the user to have SeLockMemoryPrivilege. In\n"
"  this case, --memory must be a multiple of the large page size.\n"
"\n"
"In all cases, partaked will exit with an error if the the filename\n"
"given by --file or the name given by --name already exists, unless\n"
"--force is also given.\n", // clang-format on
        file);
}

static void print_version(FILE *file) {
    const char *version = "<TBD>";
    fprintf(file, "partaked version %s\n", version);
}

static const char *progname;

static void error_exit(const char *msg) {
    if (progname) {
        fprintf(stderr, "%s: ", progname);
    }
    fputs(msg, stderr);
    exit(EXIT_FAILURE);
}

// Parse options; call exit() on error or help/version.
static void parse_options(int argc, char **argv, struct parsed_options *opts) {
    dropt_option option_defs[] = {
        // short_name long_name
        // description
        // arg_descr
        // handler dest attr extra_data

        {
            'm',
            "memory",
            "Size of shared memory",
            "{<bytes>|<kibibytes>K|<mebibytes>M|<gibibytes>G}",
            myopt_handle_size,
            &opts->memory,
        },

        {
            's',
            "socket",
            "Socket name for client connection",
            "<name>",
            dropt_handle_string,
            &opts->socket,
        },

        {
            '\0',
            "socket-fullname",
            "Full platform-specific name of socket for client connection",
            "<name>",
            dropt_handle_string,
            &opts->raw_socket,
        },

        {
            'f',
            "force",
            "Overwrite existing shared memory given by --name and/or --file",
            NULL,
            dropt_handle_bool,
            &opts->force,
        },

        {
            'n',
            "name",
            "Name of shared memory block (integer if --systemv is given)",
            "{<name>|<integer>}",
            dropt_handle_string,
            &opts->name,
        },

        {
            'F',
            "file",
            "Use shared memory backed by the given filesystem file",
            "<filename>",
            dropt_handle_string,
            &opts->file,
        },

        {
            'P',
            "posix",
            "Use POSIX shm_open(2) shared memory (default)",
            NULL,
            dropt_handle_bool,
            &opts->posix,
        },

        {
            'S',
            "systemv",
            "Use System V shmget(2) shared memory",
            NULL,
            dropt_handle_bool,
            &opts->systemv,
        },

        {
            'W',
            "windows",
            "Use Win32 named shared memory (default on Windows)",
            NULL,
            dropt_handle_bool,
            &opts->windows,
        },

        {
            'H',
            "huge-pages",
            "Use Linux huge pages with --systemv",
            NULL,
            dropt_handle_bool,
            &opts->huge_pages,
        },

        {
            'L',
            "large-pages",
            "Use Windows large pages (requires SeLockMemoryPrivilege)",
            NULL,
            dropt_handle_bool,
            &opts->large_pages,
        },

        {
            'h',
            "help",
            "Show this help and exit",
            NULL,
            dropt_handle_bool,
            &opts->help,
            dropt_attr_halt,
        },

        {
            'V',
            "version",
            "Print version and exit",
            NULL,
            dropt_handle_bool,
            &opts->version,
            dropt_attr_halt,
        },

        {0} // sentinel
    };

    int exit_code = EXIT_SUCCESS;

    dropt_context *context = dropt_new_context(option_defs);
    if (context == NULL) {
        goto error;
    }

    if (argc == 0) {
        goto error;
    }
    char **rest = dropt_parse(context, -1, &argv[1]);

    if (dropt_get_error(context) != dropt_error_none) {
        fprintf(stderr, "partaked: %s\n", dropt_get_error_message(context));
        goto error;
    }

    if (opts->help) {
        print_help_prolog(stdout);
        dropt_print_help(stdout, context, NULL);
        print_help_epilog(stdout);
        goto exit;
    }

    if (opts->version) {
        print_version(stdout);
        goto exit;
    }

    // Currently we do not have any non-option arguments.
    if (*rest != NULL) {
        fprintf(stderr, "partaked: invalid argument: %s\n", *rest);
        goto error;
    }

    dropt_free_context(context);
    return;

error:
    exit_code = EXIT_FAILURE;
exit:
    dropt_free_context(context);
    exit(exit_code);
}

static void check_socket_name(const char *name, bool use_raw,
                              struct partaked_daemon_config *config) {
    if (name == NULL) {
        error_exit("Socket must be given with option -s/--socket\n");
    }

    size_t len = strlen(name);

    if (use_raw) {
#ifdef _WIN32
        if (strncmp(name, "\\\\.\\pipe\\", 9) != 0 || len < 10 ||
            strchr(name + 9, '\\') != NULL || len > 256) {
            error_exit(
                "Windows named pipe name must consist of the prefix \"\\\\.\\pipe\\\", followed by one or more characters, none of which are backslashes, with a total length of no more than 256 characters\n");
        }
#else
        struct sockaddr_un dummy;
        if (len < 1 || len > sizeof(dummy.sun_path) - 1) {
            error_exit(
                "Unix domain socket name must not be empty and must not exceed the platform-dependent length limit\n");
        }
#endif

        config->socket = name;
        return;
    }

    // Unix domain socket path names have a lower length limit than Windows
    // named pipe names. Linux limit is 107, (some?) BSDs are 103, and
    // apparently some Unices have limits as low as 91. We choose 80 chars as
    // our limit, because it remains under 91 after prefixing with "/tmp/" (and
    // 86 is harder to remember).
    if (len < 1 || len > 80) {
        error_exit(
            "Non-raw socket name must not be empty and must not exceed 80 characters\n");
    }

    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        error_exit(
            "Non-raw socket name must not contain slashes or backslashes\n");
    }

    const char *format =
#ifndef _WIN32
        "/tmp/%s"
#else
        "\\\\.\\pipe\\%s"
#endif
        ;
    snprintf(config->socket_buf, sizeof(config->socket_buf), format, name);
    config->socket = config->socket_buf;
}

// Perform checks that can be done without calling OS API and fill struct
// partaked_daemon_config. All strings are still pointers to the static argv
// strings.
static void check_options(const struct parsed_options *opts,
                          struct partaked_daemon_config *config) {
    memset(config, 0, sizeof(struct partaked_daemon_config));

    if (opts->socket != NULL && opts->raw_socket != NULL) {
        error_exit(
            "Not both of -s/--socket and --socket-fullname may be given\n");
    }
    bool raw_sockname = opts->socket == NULL;
    const char *sockname = raw_sockname ? opts->raw_socket : opts->socket;
    check_socket_name(sockname, raw_sockname, config);

    config->size = opts->memory;
    config->force = opts->force;

    int n_types_given = (opts->posix ? 1 : 0) + (opts->systemv ? 1 : 0) +
                        (opts->file != NULL ? 1 : 0) + (opts->windows ? 1 : 0);
    if (n_types_given > 1) {
        error_exit(
            "Only one of -P/--posix, -S/--systemv, -F/--file, or -W/--windows may be given\n");
    }

    if (opts->posix) {
        config->type = PARTAKED_SHMEM_MMAP;
        config->shmem.mmap.shm_open = true;
    } else if (opts->systemv) {
        config->type = PARTAKED_SHMEM_SHMGET;
    } else if (opts->file != NULL) {
#ifndef _WIN32
        config->type = PARTAKED_SHMEM_MMAP;
#else
        config->type = PARTAKED_SHMEM_WIN32;
#endif
    } else if (opts->windows) {
        config->type = PARTAKED_SHMEM_WIN32;
    } else { // default
#ifndef _WIN32
        config->type = PARTAKED_SHMEM_MMAP;
        config->shmem.mmap.shm_open = true;
#else
        config->type = PARTAKED_SHMEM_WIN32;
#endif
    }

    // Check/convert name depending on type
    // TODO It turns out that the maximum length of even POSIX shared memory is
    // platform-dependent, so all these checks should be moved into each of the
    // shmem implementations.
    if (opts->name != NULL) {
        if (opts->posix) {
            size_t len = strlen(opts->name);
            if (opts->name[0] != '/' || len < 2 || len > 255 ||
                strchr(opts->name + 1, '/') != NULL) {
                error_exit(
                    "POSIX shared memory name must be less than 256 characters and consist of an initial slash, followed by one or more characters, none of which are slashes\n");
            }
            config->shmem.mmap.shmname = opts->name;
        } else if (opts->systemv) {
            char *end;
            errno = 0;
            long key = strtol(opts->name, &end, 10);
            if (end == opts->name || *end != '\0' || errno != 0 ||
                key > INT_MAX || key < INT_MIN) {
                // It is left to the main daemon code to check that key
                // doesn't collide with IPC_PRIVATE and that key_t is
                // actually int.
                error_exit("System V shared memory key must be an integer\n");
            }
            config->shmem.shmget.key = (int)key;
        } else if (config->type == PARTAKED_SHMEM_WIN32) {
            size_t len = strlen(opts->name);
            if (strncmp(opts->name, "Local\\", 6) != 0 || len < 7 ||
                strchr(opts->name + 6, '\\') != NULL) {
                error_exit(
                    "Windows shared memory name must consist of the prefix \"Local\\\", followed by one or more characters, none of which are backslashes\n");
            }
            config->shmem.win32.name = opts->name;
        }
    }

    if (opts->file != NULL) {
        if (opts->file[0] == '\0') {
            error_exit("Filename must not be empty\n");
        }
        if (config->type == PARTAKED_SHMEM_MMAP) {
            config->shmem.mmap.filename = opts->file;
        } else if (config->type == PARTAKED_SHMEM_WIN32) {
            config->shmem.win32.filename = opts->file;
        }
    }

    if (config->type == PARTAKED_SHMEM_SHMGET) {
        config->shmem.shmget.huge_pages = opts->huge_pages;
    } else if (opts->huge_pages) {
        error_exit(
            "-H/--huge-pages can only be used with System V shared memory\n");
    }

    if (config->type == PARTAKED_SHMEM_WIN32) {
        config->shmem.win32.large_pages = opts->large_pages;
    } else if (opts->large_pages) {
        error_exit(
            "-L/--large-pages can only be used with Windows shared memory\n");
    }
}

int main(int argc, char **argv) {
    progname = argv[0];

    struct parsed_options opts;
    memset(&opts, 0, sizeof(opts));

    parse_options(argc, argv, &opts);

    struct partaked_daemon_config config;
    memset(&config, 0, sizeof(config));

    check_options(&opts, &config);

    int ret = partaked_daemon_run(&config);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
