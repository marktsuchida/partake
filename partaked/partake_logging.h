/*
 * Logging utility functions
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

#pragma once

#include "partake_tchar.h" // TCHAR

#include <stdio.h> // snprintf
#include <stdlib.h> // size_t, wcstombs
#include <string.h> // strerror_r

#ifdef _WIN32
#   include <Windows.h> // DWORD, FormatMessage, LocalFree, etc.
#endif


#ifndef _WIN32

static inline char *partake_strerror(int errno, char *buf, size_t size) {
    if (strerror_r(errno, buf, size)) {
        snprintf(buf, size, "Unknown error %d", errno);
    }
    return buf;
}

#else // _WIN32

// zf_log does not support wide char strings. For now, we convert TCHAR* to
// char* for logging. A more rebust way would be to convert TCHAR* to UTF-8,
// and use a custom logging handler that converts back to UTF-16 in a Unicode
// build.
static inline const char *partake_strtolog(const TCHAR *s, char *buf,
        size_t size) {
#ifdef _UNICODE
    size_t r = wcstombs(buf, size, s);
    if (r == (size_t)-1) {
        snprintf(buf, size, "(string unavailable)");
    }
#else
    snprintf(buf, size, "%s", s);
#endif
    return buf;
}

// On Windows this takes Windows system error codes from GetLastError().
static inline char *partake_strerror(DWORD code, char *buf, size_t size) {
    TCHAR *msgbuf = NULL;
    if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, code,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR)&msgbuf, 0, NULL) != 0) {
        partake_strtolog(msgbuf, buf, size);
        LocalFree(msgbuf);
    }
    else {
        snprintf(buf, size, "Unknown error %u", code);
    }
    return buf;
}

#endif // _WIN32
