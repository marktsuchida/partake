/*
 * Abstract char vs wchar_t
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
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
