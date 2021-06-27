/*
 * Logging utility functions
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_tchar.h" // TCHAR

#include <stdio.h>  // snprintf
#include <stdlib.h> // size_t, wcstombs
#include <string.h> // strerror_r

#ifdef _WIN32
#include <Windows.h> // DWORD, FormatMessage, LocalFree, etc.
#endif

// zf_log does not support wide char strings. For now, we convert TCHAR* to
// char* for logging. A more rebust way would be to convert TCHAR* to UTF-8,
// and use a custom logging handler that converts back to UTF-16 in a Unicode
// build.
static inline const char *partaked_strtolog(const TCHAR *s, char *buf,
                                            size_t size) {
#if defined(_WIN32) && defined(_UNICODE)
    size_t r = wcstombs(buf, size, s);
    if (r == (size_t)-1) {
        snprintf(buf, size, "(string unavailable)");
    }
#else
    snprintf(buf, size, "%s", s);
#endif
    return buf;
}

#ifndef _WIN32

static inline char *partaked_strerror(int errno, char *buf, size_t size) {
    if (strerror_r(errno, buf, size)) {
        snprintf(buf, size, "Unknown error %d", errno);
    }
    return buf;
}

#else // _WIN32

// On Windows this takes Windows system error codes from GetLastError().
static inline char *partaked_strerror(DWORD code, char *buf, size_t size) {
    TCHAR *msgbuf = NULL;
    if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                          FORMAT_MESSAGE_FROM_SYSTEM |
                          FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPTSTR)&msgbuf, 0, NULL) != 0) {
        partaked_strtolog(msgbuf, buf, size);
        LocalFree(msgbuf);
    } else {
        snprintf(buf, size, "Unknown error %u", code);
    }
    return buf;
}

#endif // _WIN32
