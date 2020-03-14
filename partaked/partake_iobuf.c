/*
 * Reference-counted buffers for request/response
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

#include "partake_iobuf.h"
#include "partake_malloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


static struct partake_iobuf *freelist;


struct partake_iobuf *partake_iobuf_create(size_t size) {
    struct partake_iobuf *ret;
    if (size == PARTAKE_IOBUF_STD_SIZE && freelist != NULL) {
        ret = freelist;
        freelist = ret->md.next_free;
    }
    else {
        ret = partake_malloc(sizeof(struct partake_iobuf));
        ret->uvbuf.base = partake_malloc(size);
        ret->uvbuf.len = size;
    }

    ret->md.refcount = 0;

    return ret;
}


void partake_iobuf_destroy(struct partake_iobuf *buf) {
    assert (buf->md.refcount == 0);

    if (buf->uvbuf.len == PARTAKE_IOBUF_STD_SIZE) {
        buf->md.next_free = freelist;
        freelist = buf;
    }
    else {
        partake_free(buf->uvbuf.base);
        partake_free(buf);
    }
}


void partake_iobuf_release_freelist(void) {
    while (freelist != NULL) {
        struct partake_iobuf *b = freelist;
        freelist = b->md.next_free;
        partake_free(b->uvbuf.base);
        partake_free(b);
    }
}
