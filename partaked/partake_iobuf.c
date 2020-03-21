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

#include "prefix.h"

#include "partake_iobuf.h"
#include "partake_malloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


#define BUFFER_ALIGNMENT 8 // 8 is sufficient for our flatbuffers


static struct partake_iobuf *freelist;


struct partake_iobuf *partake_iobuf_create(size_t size) {
    struct partake_iobuf *iobuf;
    if (size == PARTAKE_IOBUF_STD_SIZE && freelist != NULL) {
        iobuf = freelist;
        freelist = iobuf->management.next_free;
        iobuf->management.next_free = NULL;
    }
    else {
        size_t header_size = sizeof(struct partake_iobuf);
        header_size = (header_size + BUFFER_ALIGNMENT - 1) &
            ~(BUFFER_ALIGNMENT - 1);

        iobuf = partake_malloc(header_size + size);
        iobuf->addr_to_free = iobuf;
        iobuf->buffer = (char *)iobuf + header_size;
        iobuf->capacity = size;
    }

    iobuf->management.refcount = 1;

    return iobuf;
}


void partake_iobuf_destroy(struct partake_iobuf *iobuf) {
    assert (iobuf->management.refcount == 0);
    assert (iobuf->management.next_free == 0);

    if (iobuf->capacity == PARTAKE_IOBUF_STD_SIZE) {
        iobuf->management.next_free = freelist;
        freelist = iobuf;
    }
    else {
        partake_free(iobuf->addr_to_free);
    }
}


void partake_iobuf_release_freelist(void) {
    while (freelist != NULL) {
        struct partake_iobuf *iobuf = freelist;
        freelist = iobuf->management.next_free;
        partake_free(iobuf->addr_to_free);
    }
}
