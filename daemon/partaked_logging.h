/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#ifdef _WIN32

// On Windows this takes Windows system error codes from GetLastError().
static inline char *partaked_strerror(DWORD code, char *buf, size_t size) {
    char *msgbuf = NULL;
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       &msgbuf, 0, NULL) != 0) {
        snprintf(buf, size, "%s", msgbuf);
        LocalFree(msgbuf);
    } else {
        snprintf(buf, size, "Unknown error %u", code);
    }
    return buf;
}

#else // _WIN32

// POSIX
static inline char *partaked_strerror(int erno, char *buf, size_t size) {
    if (strerror_r(erno, buf, size)) {
        snprintf(buf, size, "Unknown error %d", erno);
    }
    return buf;
}

#endif // _WIN32
