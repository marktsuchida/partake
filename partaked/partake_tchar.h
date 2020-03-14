/*
 * Abstract char vs wchar_t
 */

#pragma once

#ifdef _WIN32

    // Guard against inconsistent UNICODE (Win32 A vs W) and _UNICODE (tchar
    // and C runtime) settings, so that we only need to deal with 2 cases.
#   if defined(UNICODE) != defined(_UNICODE)
#       error Inconsistent definition of UNICODE vs _UNICODE
#   endif

#   include <tchar.h>

#   define TEXT(x) _TEXT(x)

#   define fputts _fputts
#   define ftprintf _ftprintf
#   define sntprintf _sntprintf
#   define tcslen _tcslen
#   define tcschr _tcschr
#   define tcsncmp _tcsncmp
#   define tcstol _tcstol

#else // _WIN32

typedef char TCHAR

#   define TEXT(x) x

#   define fputts fputs
#   define ftprintf fprintf
#   define sntprintf snprintf
#   define tcslen strlen
#   define tcschr strchr
#   define tcsncmp strncmp
#   define tcstol strtol

#endif
