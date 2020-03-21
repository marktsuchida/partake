/*
 * Abstract char vs wchar_t
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

#include <stdio.h>

#ifdef _WIN32

    // Guard against inconsistent UNICODE (Win32 A vs W) and _UNICODE (tchar
    // and C runtime) settings, so that we only need to deal with 2 cases.
#   if defined(UNICODE) != defined(_UNICODE)
#       error Inconsistent definition of UNICODE vs _UNICODE
#   endif

#   define WIN32_LEAN_AND_MEAN
#   include <tchar.h>
#   include <Windows.h>

#   define PARTAKE_TEXT(x) _TEXT(x)

#   define fputts _fputts
#   define ftprintf _ftprintf
#   define sntprintf _sntprintf
#   define tcslen _tcslen
#   define tcschr _tcschr
#   define tcsncmp _tcsncmp
#   define tcstol _tcstol

static inline const char *partake_tstrtoutf8(const TCHAR *s, char *buf,
        size_t size) {
#ifdef UNICODE
    WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, (int)size, NULL, NULL);
#else
    snprintf(buf, size, "%s", s);
#endif
    return buf;
}

#else // _WIN32

typedef char TCHAR;

#   define PARTAKE_TEXT(x) x

#   define fputts fputs
#   define ftprintf fprintf
#   define sntprintf snprintf
#   define tcslen strlen
#   define tcschr strchr
#   define tcsncmp strncmp
#   define tcstol strtol

static inline const char *partake_tstrtoutf8(const TCHAR *s, char *buf,
        size_t size) {
    snprintf(buf, size, "%s", s);
    return buf;
}

#endif
