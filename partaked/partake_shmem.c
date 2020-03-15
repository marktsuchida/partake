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

#include "partake_logging.h"
#include "partake_malloc.h"
#include "partake_shmem.h"
#include "partake_tchar.h"

#include <zf_log.h>

#ifndef _WIN32
#   include <errno.h>
#   include <fcntl.h>
#   include <unistd.h>
#else
#   include <Bcrypt.h>
#endif

#include <stdlib.h>


#define NAME_INFIX PARTAKE_TEXT("partake-shmem-")
#define DEV_URANDOM "/dev/urandom"


static char *alloc_random_bytes(size_t size) {
    char *buf = partake_malloc(size);

#ifndef _WIN32
    char emsg[1024];
    errno = 0;
    int fd = open(DEV_URANDOM, O_RDONLY);
    if (fd < 0) {
        ZF_LOGF("open: " DEV_URANDOM ": %s",
                partake_strerror(errno, emsg, sizeof(emsg)));
        abort();
    }

    errno = 0;
    ssize_t bytes = read(fd, buf, size);
    if (bytes < size) {
        ZF_LOGF("read: " DEV_URANDOM ": %s",
                partake_strerror(errno, emsg, sizeof(emsg)));
        abort();
    }

    errno = 0;
    if (close(fd) != 0) {
        ZF_LOGF("close: " DEV_URANDOM ": %s",
                partake_strerror(errno, emsg, sizeof(emsg)));
    }
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
TCHAR *partake_alloc_random_name(TCHAR *prefix, size_t random_len) {
    size_t len = tcslen(prefix) + tcslen(NAME_INFIX) + random_len;
    TCHAR *ret = partake_malloc(sizeof(TCHAR) * (len + 1));

    int prefix_infix_len =
        sntprintf(ret, len, PARTAKE_TEXT("%s") NAME_INFIX, prefix);

    size_t randbufsize = (random_len + 1) / 2;
    char *randbuf = alloc_random_bytes(randbufsize);

    // Now convert (up to) randbufsize random bytes to random_len hex digits
    char *byte = randbuf;
    TCHAR *p = ret + prefix_infix_len;
    size_t space_left = len + 1 - prefix_infix_len;
    while (space_left > 1) {
        int n = sntprintf(p, space_left, "%hhx", *byte++);
        p += n;
        space_left -= n;

        if (space_left == 0) { // Win32 _sntprintf doesn't null-terminate
            *--p = PARTAKE_TEXT('\0');
        }
    }

    partake_free(randbuf);

    return ret;
}
