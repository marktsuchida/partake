/*
 * Random string generation
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_random.h"

#include "partaked_logging.h"
#include "partaked_malloc.h"
#include "partaked_tchar.h"

#include <zf_log.h>

#ifdef _WIN32
#   include <Bcrypt.h>
#endif

#ifdef __linux__
#   include <bsd/stdlib.h> // arc4random
#endif

#include <stdlib.h>


#define NAME_INFIX PARTAKED_TEXT("partake-")


static char *alloc_random_bytes(size_t size) {
    char *buf = partaked_malloc(size);

#ifndef _WIN32
    // Not POSIX but available on Linux, macOS, most BSDs
    arc4random_buf(buf, size);
#else
    NTSTATUS ret = BCryptGenRandom(NULL, buf, size,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (ret != 0) {
        ZF_LOGF("Random name generation failed");
        abort();
    }
#endif

    return buf;
}


int partaked_generate_random_int(void) {
    int ret;
    char *buf = alloc_random_bytes(sizeof(int));
    memcpy(&ret, buf, sizeof(int));
    partaked_free(buf);
    return ret;
}


// Return a partaked_malloc()ed tstring consisting of prefix followed by
// NAME_INFIX followed by random_len characters of random hex digits.
TCHAR *partaked_alloc_random_name(TCHAR *prefix, size_t random_len,
        size_t max_total_len) {
    size_t prefix_infix_len = tcslen(prefix) + tcslen(NAME_INFIX);
    if (prefix_infix_len >= max_total_len) {
        ZF_LOGF("Random name total length too short to fit random chars");
        abort();
    }

    size_t len = prefix_infix_len + random_len;
    if (len > max_total_len) {
        len = max_total_len;
        random_len = len - prefix_infix_len;
    }

    TCHAR *ret = partaked_malloc(sizeof(TCHAR) * (len + 1));

    sntprintf(ret, len, PARTAKED_TEXT("%s") NAME_INFIX, prefix);

    size_t randbufsize = (random_len + 1) / 2;
    char *randbuf = alloc_random_bytes(randbufsize);

    // Now convert (up to) randbufsize random bytes to random_len hex digits
    char *byte = randbuf;
    TCHAR *p = ret + prefix_infix_len;
    size_t space_left = len + 1 - prefix_infix_len;
    while (space_left > 1) {
        sntprintf(p, space_left, "%02hhx", *byte++);
        size_t n = space_left > 1 ? 2 : 1;
        p += n;
        space_left -= n;

        if (space_left == 0) { // Win32 _sntprintf doesn't null-terminate
            *--p = PARTAKED_TEXT('\0');
        }
    }

    partaked_free(randbuf);

    return ret;
}
