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

#include "partake_daemon.h"
#include "partake_tchar.h"

#include <dropt.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// The prefix myopt_ is used in this file for local extensions to dropt.
// All such symbols follow dropt's support for wide char handling.

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
    dropt_char **socket;
    dropt_bool force;
    dropt_char **name;
    dropt_char **file;
    dropt_bool posix;
    dropt_bool systemv;
    dropt_bool windows;
    dropt_bool huge_pages;
    dropt_bool large_pages;
    dropt_bool help;
    dropt_bool version;
};


static void print_help_prolog(FILE *file) {
    fputts(TEXT("Usage: partaked -m <size> -s <name> [more options]\n"), file);
    fputts(TEXT("\n"), file);
    fputts(TEXT("Options:\n"), file);
}


static void print_help_epilog(FILE *file) {
    fputts(
TEXT("Client connection:\n")
TEXT("  On POSIX systems, a UNIX domain socket (AF_UNIX) is used. The name\n")
TEXT("  passed to --socket should be an ordinary file path. On Windows, a\n")
TEXT("  named pipe is used; the name must begin with \"\\\\.\\pipe\\\". In\n")
TEXT("  both cases, the same name should be passed to clients so that they\n")
TEXT("  can connect.\n")
TEXT("\n")
TEXT("POSIX shared memory:\n")
TEXT("  [--posix] [--name=/myshmem]: Create with shm_open(2) and map with\n")
TEXT("      mmap(2). If name is given it should start with a slash and\n")
TEXT("      contain no more slashes.\n")
TEXT("  --systemv [--name=key]: Create with shmget(2) and map with shmat(2).\n")
TEXT("      If name is given it must be an integer key.\n")
TEXT("  --file=myfile: Create with open(2) and map with mmap(2). The --name\n")
TEXT("      option is ignored.\n")
TEXT("  Not all of the above may be available on a given UNIX-like system.\n")
TEXT("  On Linux, huge pages can be allocated either by using --file with a\n")
TEXT("  location in a mounted hugetlbfs or by giving --huge-pages with\n")
TEXT("  --systemv. In both cases, --memory must be a multiple of the huge\n")
TEXT("  page size.\n")
TEXT("\n")
TEXT("Windows shared memory:\n")
TEXT("  [--windows] [--name=Local\\myshmem]: A named file mapping backed by\n")
TEXT("      the system paging file is created. If name is given it should\n")
TEXT("      start with \"Local\\\" and contain no further backslashes.\n")
TEXT("  --file=myfile [--name=Local\\myshmem]: A named file mapping backed\n")
TEXT("      by the given file is created. Usage of --name is the same as\n")
TEXT("      with --windows.\n")
TEXT("  On Windows, --large-pages can be specified with either of the above\n")
TEXT("  options. This requires the user to have SeLockMemoryPrivilege. In\n")
TEXT("  this case, --memory must be a multiple of the large page size.\n")
TEXT("\n")
TEXT("In all cases, partaked will exit with an error if the the filename\n")
TEXT("given by --file or the name given by --name already exists, unless\n")
TEXT("--force is also given. An exception to this rule is Windows shared\n")
TEXT("memory: using a name that exists may result in attaching to an\n")
TEXT("existing mapping, with highly undesirable consequences.\n"),
    file);
}


static void print_version(FILE *file) {
    const TCHAR *version = "<TBD>";
    ftprintf(file, TEXT("partaked version %s\n"), version);
}


static const TCHAR *progname;

static void error_exit(const TCHAR *msg) {
    if (progname) {
        ftprintf(stderr, TEXT("%s: "), progname);
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

        { TEXT('m'), TEXT("memory"),
            TEXT("Size of shared memory"),
            TEXT("{<bytes>|<kibibytes>K|<mebibytes>M|<gibibytes>G}"),
            myopt_handle_size, &opts->memory, },

        { TEXT('s'), TEXT("socket"),
            TEXT("Name of UNIX domain socket or Win32 named pipe for client ")
                TEXT("connection"),
            TEXT("<name>"),
            dropt_handle_string, &opts->socket, },

        { TEXT('f'), TEXT("force"),
            TEXT("Overwrite existing shared memory given by --name and/or ")
                TEXT("--file"),
            NULL,
            dropt_handle_bool, &opts->force, },

        { TEXT('n'), TEXT("name"),
            TEXT("Name of shared memory block (integer if --systemv is given)"),
            TEXT("{<name>|<integer>}"),
            dropt_handle_string, &opts->name, },

        { TEXT('F'), TEXT("file"),
            TEXT("Use shared memory backed by the given filesystem file"),
            TEXT("<filename>"),
            dropt_handle_string, &opts->file, },

        { TEXT('P'), TEXT("posix"),
            TEXT("Use POSIX shm_open(2) shared memory (default)"),
            NULL,
            dropt_handle_bool, &opts->posix, },

        { TEXT('S'), TEXT("systemv"),
            TEXT("Use System V shmget(2) shared memory"),
            NULL,
            dropt_handle_bool, &opts->systemv, },

        { TEXT('W'), TEXT("windows"),
            TEXT("Use Win32 named shared memory (default on Windows)"),
            NULL,
            dropt_handle_bool, &opts->windows, },

        { TEXT('H'), TEXT("huge-pages"),
            TEXT("Use Linux huge pages with --systemv"),
            NULL,
            dropt_handle_bool, &opts->huge_pages, },

        { TEXT('L'), TEXT("large-pages"),
            TEXT("Use Windows large pages (requires SeLockMemoryPrivilege)"),
            NULL,
            dropt_handle_bool, &opts->large_pages, },

        { TEXT('h'), TEXT("help"), TEXT("Show this help and exit"), NULL,
            dropt_handle_bool, &opts->help, dropt_attr_halt, },

        { TEXT('V'), TEXT("version"), TEXT("Print version and exit"), NULL,
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
        ftprintf(stderr, TEXT("partaked: %s\n"),
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
        ftprintf(stderr, TEXT("partaked: invalid argument: %s\n"), *rest);
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
        error_exit(TEXT("Socket must be given with option -s/--socket\n"));
    }
#ifdef _WIN32
    if (tcsncmp(*opts->socket, TEXT("\\\\.\\pipe\\"), 9) != 0) {
        error_exit(TEXT("Socket name must begin with \"\\\\.\\pipe\\\"\n"));
    }
#endif
    config->socket = *opts->socket;

    config->size = opts->memory;
    config->force = opts->force;

    int n_types_given =
        (opts->posix ? 1 : 0) + (opts->systemv ? 1 : 0) +
        (opts->file != NULL ? 1 : 0) + (opts->windows ? 1 : 0);
    if (n_types_given > 1) {
        error_exit(TEXT("Only one of -P/--posix, -S/--systemv, -F/--file, or ")
                TEXT("-W/--windows may be given\n"));
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
            size_t len = tcslen(*opts->name);
            if ((*opts->name)[0] != TEXT('/') || len < 2 || len > 255 ||
                    tcschr(*opts->name + 1, TEXT('/')) != NULL) {
                error_exit(TEXT("POSIX shared memory name must be less ")
                        TEXT("than 256 characters and consist of an ")
                        TEXT("initial slash, followed by one or more ")
                        TEXT("characters, none of which are slashes\n"));
            }
            config->shmem.mmap.shmname = *opts->name;
        }
        else if (opts->systemv) {
            TCHAR *end;
            errno = 0;
            long key = tcstol(*opts->name, &end, 10);
            if (end == *opts->name || *end != TEXT('\0') ||
                    errno != 0 || key > INT_MAX || key < INT_MIN) {
                // It is left to the main daemon code to check that key
                // doesn't collide with IPC_PRIVATE and that key_t is
                // actually int.
                error_exit(TEXT("System V shared memory key must be an ")
                        TEXT("integer\n"));
            }
            config->shmem.shmget.key = (int)key;
        }
        else if (config->type == PARTAKE_SHMEM_WIN32) {
            size_t len = tcslen(*opts->name);
            if (tcsncmp(*opts->name, TEXT("Local\\"), 6) != 0 ||
                    len < 7 || tcschr(*opts->name + 6, TEXT('\\')) != NULL) {
                error_exit(TEXT("Windows shared memory name must consist of ")
                        TEXT("the prefix \"Local\\\", followed by one or ")
                        TEXT("more characters, none of which are ")
                        TEXT("backslashes\n"));
            }
            config->shmem.win32.name = *opts->name;
        }
    }

    if (opts->file != NULL) {
        if ((*opts->file)[0] == TEXT('\0')) {
            error_exit(TEXT("Filename must not be empty\n"));
        }
        if (config->type == PARTAKE_SHMEM_MMAP) {
            config->shmem.mmap.filename = *opts->file;
        }
        else if (config->type == PARTAKE_SHMEM_WIN32) {
            config->shmem.win32.filename = *opts->file;
        }
    }

    if (config->type == PARTAKE_SHMEM_SHMGET) {
        config->shmem.shmget.huge_pages = opts->huge_pages;
    }
    else if (opts->huge_pages) {
        error_exit(TEXT("-H/--huge-pages can only be used with System V ")
                TEXT("shared memory\n"));
    }

    if (config->type == PARTAKE_SHMEM_WIN32) {
        config->shmem.win32.large_pages = opts->large_pages;
    }
    else if (opts->large_pages) {
        error_exit(TEXT("-L/--large-pages can only be used with Windows ")
                TEXT("shared memory\n"));
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

    // int ret = run_daemon(&config);

    return EXIT_SUCCESS;
}
