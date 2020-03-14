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

#pragma once

#include <uv.h>

#include <stddef.h>


/*
 * The main idea is to process requests without making copies of the
 * FlatBuffers messages or their content. We pass a uv_buf_t * to libuv and
 * receive it back when messages have been received. By placing our metadata
 * after the uv_buf_t struct, we can immediately access it. Request handlers
 * can retain the buffer by incrementing the refcount if they need to finish
 * handling the request at a later time.
 *
 * In the case of a message spanning more than one buffer, we make a copy into
 * a dedicated partake_iobuf of sufficient size.
 *
 * We further improve efficiency by keeping "destroyed" buffers in a free list
 * for reuse.
 *
 * Alignment. Without special directives, the largest scalar contained in a
 * FlatBuffers serialization is 8 bytes. To preserve alignment in the stream,
 * we need to ensure that all message lengths are round up to a multiple of 8.
 * The receiver then only needs to read into 8-byte-aligned buffers. Recovery
 * is needed when the kernel decides to interrupt a read mid-message.
 */


// Buffers of this size are recycled
#define PARTAKE_IOBUF_STD_SIZE 65536


struct partake_iobuf {
    uv_buf_t uvbuf; // Must be at offset 0
    union {
        size_t refcount;
        struct partake_iobuf *next_free; // Freelist
    } md;
};


static inline struct partake_iobuf *partake_iobuf_from_uvbuf(uv_buf_t *b) {
    return (struct partake_iobuf *)b;
}


static inline uv_buf_t *partake_iobuf_get_uvbuf(struct partake_iobuf *b) {
    return (uv_buf_t *)b;
}


struct partake_iobuf *partake_iobuf_create(size_t size);

void partake_iobuf_destroy(struct partake_iobuf *buf);

void partake_iobuf_release_freelist(void);
