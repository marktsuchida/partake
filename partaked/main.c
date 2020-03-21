/*
 * Command-line interface to partaked
 *
 *
 * Copyright (C) 2020, The Board of Regents of the University of Wisconsin
 * System
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "prefix.h"

#include "partake_daemon.h"
#include "partake_tchar.h"

#include <dropt.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PTXT(x) PARTAKE_TEXT(x)


/*
 * The prefix myopt_ is used in this file for local extensions to dropt.
 * All such symbols follow dropt's support for wide char handling.
 *
 * We do not bother to use partake_malloc() for option parsing (dropt doesn't
 * support replacing malloc()).
 */

// We need long long to ensure 64-bit sizes fit (Windows is LLP64).
#ifdef DROPT_USE_WCHAR
#   define myopt_strtoll wcstoll
#else
#   define myopt_strtoll strtoll
#endif


// Parse an option argument with optional K/M/G suffix (case sensitive)
static dropt_error myopt_handle_size(dropt_context *context,
        const dropt_option *option, const dropt_char *arg, void *dest) {
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
    long long n = myopt_strtoll(arg, &end, 10);
    if (end == arg || n < 0) {
        return dropt_error_mismatch;
    }
    if (errno == ERANGE) {
        return dropt_error_overflow;
    }
    else if (errno != 0) {
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
    fputts(PTXT("Usage: partaked -m <size> -s <name> [more options]\n"), file);
    fputts(PTXT("\n"), file);
    fputts(PTXT("Options:\n"), file);
}


static void print_help_epilog(FILE *file) {
    fputts(
PTXT("Client connection:\n")
PTXT("  On POSIX systems, a UNIX domain socket (AF_UNIX) is used. The name\n")
PTXT("  passed to --socket should be an ordinary file path. On Windows, a\n")
PTXT("  named pipe is used; the name must begin with \"\\\\.\\pipe\\\". In\n")
PTXT("  both cases, the same name should be passed to clients so that they\n")
PTXT("  can connect.\n")
PTXT("\n")
PTXT("POSIX shared memory:\n")
PTXT("  [--posix] [--name=/myshmem]: Create with shm_open(2) and map with\n")
PTXT("      mmap(2). If name is given it should start with a slash and\n")
PTXT("      contain no more slashes.\n")
PTXT("  --systemv [--name=key]: Create with shmget(2) and map with shmat(2).\n")
PTXT("      If name is given it must be an integer key.\n")
PTXT("  --file=myfile: Create with open(2) and map with mmap(2). The --name\n")
PTXT("      option is ignored.\n")
PTXT("  Not all of the above may be available on a given UNIX-like system.\n")
PTXT("  On Linux, huge pages can be allocated either by using --file with a\n")
PTXT("  location in a mounted hugetlbfs or by giving --huge-pages with\n")
PTXT("  --systemv. In both cases, --memory must be a multiple of the huge\n")
PTXT("  page size.\n")
PTXT("\n")
PTXT("Windows shared memory:\n")
PTXT("  [--windows] [--name=Local\\myshmem]: A named file mapping backed by\n")
PTXT("      the system paging file is created. If name is given it should\n")
PTXT("      start with \"Local\\\" and contain no further backslashes.\n")
PTXT("  --file=myfile [--name=Local\\myshmem]: A named file mapping backed\n")
PTXT("      by the given file is created. Usage of --name is the same as\n")
PTXT("      with --windows.\n")
PTXT("  On Windows, --large-pages can be specified with either of the above\n")
PTXT("  options. This requires the user to have SeLockMemoryPrivilege. In\n")
PTXT("  this case, --memory must be a multiple of the large page size.\n")
PTXT("\n")
PTXT("In all cases, partaked will exit with an error if the the filename\n")
PTXT("given by --file or the name given by --name already exists, unless\n")
PTXT("--force is also given.\n"),
    file);
}


static void print_version(FILE *file) {
    const TCHAR *version = "<TBD>";
    ftprintf(file, PTXT("partaked version %s\n"), version);
}


static const TCHAR *progname;

static void error_exit(const TCHAR *msg) {
    if (progname) {
        ftprintf(stderr, PTXT("%s: "), progname);
    }
    fputts(msg, stderr);
    exit(EXIT_FAILURE);
}


// Parse options; call exit() on error or help/version.
static void parse_options(int argc, TCHAR **argv, struct parsed_options *opts) {
    dropt_option option_defs[] = {
        // short_name long_name
        // description
        // arg_descr
        // handler dest attr extra_data

        { PTXT('m'), PTXT("memory"),
            PTXT("Size of shared memory"),
            PTXT("{<bytes>|<kibibytes>K|<mebibytes>M|<gibibytes>G}"),
            myopt_handle_size, &opts->memory, },

        { PTXT('s'), PTXT("socket"),
            PTXT("Name of UNIX domain socket or Win32 named pipe for client ")
                PTXT("connection"),
            PTXT("<name>"),
            dropt_handle_string, &opts->socket, },

        { PTXT('f'), PTXT("force"),
            PTXT("Overwrite existing shared memory given by --name and/or ")
                PTXT("--file"),
            NULL,
            dropt_handle_bool, &opts->force, },

        { PTXT('n'), PTXT("name"),
            PTXT("Name of shared memory block (integer if --systemv is given)"),
            PTXT("{<name>|<integer>}"),
            dropt_handle_string, &opts->name, },

        { PTXT('F'), PTXT("file"),
            PTXT("Use shared memory backed by the given filesystem file"),
            PTXT("<filename>"),
            dropt_handle_string, &opts->file, },

        { PTXT('P'), PTXT("posix"),
            PTXT("Use POSIX shm_open(2) shared memory (default)"),
            NULL,
            dropt_handle_bool, &opts->posix, },

        { PTXT('S'), PTXT("systemv"),
            PTXT("Use System V shmget(2) shared memory"),
            NULL,
            dropt_handle_bool, &opts->systemv, },

        { PTXT('W'), PTXT("windows"),
            PTXT("Use Win32 named shared memory (default on Windows)"),
            NULL,
            dropt_handle_bool, &opts->windows, },

        { PTXT('H'), PTXT("huge-pages"),
            PTXT("Use Linux huge pages with --systemv"),
            NULL,
            dropt_handle_bool, &opts->huge_pages, },

        { PTXT('L'), PTXT("large-pages"),
            PTXT("Use Windows large pages (requires SeLockMemoryPrivilege)"),
            NULL,
            dropt_handle_bool, &opts->large_pages, },

        { PTXT('h'), PTXT("help"), PTXT("Show this help and exit"), NULL,
            dropt_handle_bool, &opts->help, dropt_attr_halt, },

        { PTXT('V'), PTXT("version"), PTXT("Print version and exit"), NULL,
            dropt_handle_bool, &opts->version, dropt_attr_halt, },

        { 0 } // sentinel
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
        ftprintf(stderr, PTXT("partaked: %s\n"),
                dropt_get_error_message(context));
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
        ftprintf(stderr, PTXT("partaked: invalid argument: %s\n"), *rest);
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


// Perform checks that can be done without calling OS API and fill struct
// partake_daemon_config. All strings are still pointers to the static argv
// strings.
static void check_options(const struct parsed_options *opts,
        struct partake_daemon_config *config) {
    memset(config, 0, sizeof(struct partake_daemon_config));

    if (opts->socket == NULL) {
        error_exit(PTXT("Socket must be given with option -s/--socket\n"));
    }
#ifdef _WIN32
    if (tcsncmp(opts->socket, PTXT("\\\\.\\pipe\\"), 9) != 0) {
        error_exit(PTXT("Socket name must begin with \"\\\\.\\pipe\\\"\n"));
    }
#endif
    config->socket = opts->socket;

    config->size = opts->memory;
    config->force = opts->force;

    int n_types_given =
        (opts->posix ? 1 : 0) + (opts->systemv ? 1 : 0) +
        (opts->file != NULL ? 1 : 0) + (opts->windows ? 1 : 0);
    if (n_types_given > 1) {
        error_exit(PTXT("Only one of -P/--posix, -S/--systemv, -F/--file, or ")
                PTXT("-W/--windows may be given\n"));
    }

    if (opts->posix) {
        config->type = PARTAKE_SHMEM_MMAP;
        config->shmem.mmap.shm_open = true;
    }
    else if (opts->systemv) {
        config->type = PARTAKE_SHMEM_SHMGET;
    }
    else if (opts->file != NULL) {
#ifndef _WIN32
        config->type = PARTAKE_SHMEM_MMAP;
#else
        config->type = PARTAKE_SHMEM_WIN32;
#endif
    }
    else if (opts->windows) {
        config->type = PARTAKE_SHMEM_WIN32;
    }
    else { // default
#ifndef _WIN32
        config->type = PARTAKE_SHMEM_MMAP;
        config->shmem.mmap.shm_open = true;
#else
        config->type = PARTAKE_SHMEM_WIN32;
#endif
    }

    // Check/convert name depending on type
    if (opts->name != NULL) {
        if (opts->posix) {
            size_t len = tcslen(opts->name);
            if (opts->name[0] != PTXT('/') || len < 2 || len > 255 ||
                    tcschr(opts->name + 1, PTXT('/')) != NULL) {
                error_exit(PTXT("POSIX shared memory name must be less ")
                        PTXT("than 256 characters and consist of an ")
                        PTXT("initial slash, followed by one or more ")
                        PTXT("characters, none of which are slashes\n"));
            }
            config->shmem.mmap.shmname = opts->name;
        }
        else if (opts->systemv) {
            TCHAR *end;
            errno = 0;
            long key = tcstol(opts->name, &end, 10);
            if (end == opts->name || *end != PTXT('\0') ||
                    errno != 0 || key > INT_MAX || key < INT_MIN) {
                // It is left to the main daemon code to check that key
                // doesn't collide with IPC_PRIVATE and that key_t is
                // actually int.
                error_exit(PTXT("System V shared memory key must be an ")
                        PTXT("integer\n"));
            }
            config->shmem.shmget.key = (int)key;
        }
        else if (config->type == PARTAKE_SHMEM_WIN32) {
            size_t len = tcslen(opts->name);
            if (tcsncmp(opts->name, PTXT("Local\\"), 6) != 0 ||
                    len < 7 || tcschr(opts->name + 6, PTXT('\\')) != NULL) {
                error_exit(PTXT("Windows shared memory name must consist of ")
                        PTXT("the prefix \"Local\\\", followed by one or ")
                        PTXT("more characters, none of which are ")
                        PTXT("backslashes\n"));
            }
            config->shmem.win32.name = opts->name;
        }
    }

    if (opts->file != NULL) {
        if (opts->file[0] == PTXT('\0')) {
            error_exit(PTXT("Filename must not be empty\n"));
        }
        if (config->type == PARTAKE_SHMEM_MMAP) {
            config->shmem.mmap.filename = opts->file;
        }
        else if (config->type == PARTAKE_SHMEM_WIN32) {
            config->shmem.win32.filename = opts->file;
        }
    }

    if (config->type == PARTAKE_SHMEM_SHMGET) {
        config->shmem.shmget.huge_pages = opts->huge_pages;
    }
    else if (opts->huge_pages) {
        error_exit(PTXT("-H/--huge-pages can only be used with System V ")
                PTXT("shared memory\n"));
    }

    if (config->type == PARTAKE_SHMEM_WIN32) {
        config->shmem.win32.large_pages = opts->large_pages;
    }
    else if (opts->large_pages) {
        error_exit(PTXT("-L/--large-pages can only be used with Windows ")
                PTXT("shared memory\n"));
    }
}


#ifdef _WIN32
#   define main _tmain
#endif
int main(int argc, TCHAR **argv)
{
    progname = argv[0];

    struct parsed_options opts;
    memset(&opts, 0, sizeof(opts));

    parse_options(argc, argv, &opts);

    struct partake_daemon_config config;
    memset(&config, 0, sizeof(config));

    check_options(&opts, &config);

    int ret = partake_daemon_run(&config);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
