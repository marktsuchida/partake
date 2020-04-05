/*
 * Shared memory allocation for partaked
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

#include "partake_logging.h"
#include "partake_malloc.h"
#include "partake_shmem.h"
#include "partake_tchar.h"

#include <zf_log.h>

#ifdef _WIN32
#   include <Bcrypt.h>
#endif

#ifdef __linux__
#   include <bsd/stdlib.h> // arc4random
#endif

#include <stdlib.h>


#define NAME_INFIX PARTAKE_TEXT("partake-")


static char *alloc_random_bytes(size_t size) {
    char *buf = partake_malloc(size);

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


int partake_generate_random_int(void) {
    int ret;
    char *buf = alloc_random_bytes(sizeof(int));
    memcpy(&ret, buf, sizeof(int));
    partake_free(buf);
    return ret;
}


// Return a partake_malloc()ed tstring consisting of prefix followed by
// NAME_INFIX followed by random_len characters of random hex digits.
TCHAR *partake_alloc_random_name(TCHAR *prefix, size_t random_len,
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

    TCHAR *ret = partake_malloc(sizeof(TCHAR) * (len + 1));

    sntprintf(ret, len, PARTAKE_TEXT("%s") NAME_INFIX, prefix);

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
            *--p = PARTAKE_TEXT('\0');
        }
    }

    partake_free(randbuf);

    return ret;
}
