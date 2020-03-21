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
#define PARTAKE_IOBUF_STD_SIZE (49152 - 32)


/*
 * Overview of buffer handling with libuv io:
 *
 * The iobufs are reference counted, and we retain while using for io.
 *
 * When reading, we pass a standard-sized buffer via the alloc_cb. In the read
 * callback, we will find zero or more messages in this buffer, with the last
 * one potentially being partial. For each complete message (easily determined
 * by their offset within the buffer and the size prefix of the message), we
 * handle requests; if any request needs to be suspended, it the request
 * handler retains the iobuf.
 *
 * If the last message is incomplete, we have two options: (1) pass this buffer
 * to libuv again, or (2) copy the partial message to the front of a new iobuf
 * and pass it to libuv. In both cases, we set the uv_buf_t to point to the
 * unfilled portion of the buffer.
 *
 * If the partial message has a complete size prefix indicating that it will
 * not fit in the current buffer, we take approach (2). Otherwise, we
 * heuristically choose between (1) and (2) to try to minimize copying.
 *
 * When writing, we use a standard-sized buffer for each message.
 *
 * The iobuf(s) currently being read into or written from by libuv are retained
 * by our connection object, so that we can recover them when the io completes.
 */


struct partake_iobuf {
    // We place this struct within the same partake_malloc()ed block as the
    // buffer itself. Passing addr_to_free to partake_free() frees both the
    // buffer and the struct partake_iobuf.
    void *addr_to_free;

    void *buffer; // Start of data buffer
    size_t capacity; // Capacity of data buffer

    union {
        size_t refcount;
        struct partake_iobuf *next_free; // Freelist
    } management;
};


struct partake_iobuf *partake_iobuf_create(size_t size);

// Do not call directly; use partake_iobuf_release().
void partake_iobuf_destroy(struct partake_iobuf *iobuf);

static inline void partake_iobuf_retain(struct partake_iobuf *iobuf) {
    ++iobuf->management.refcount;
}

static inline void partake_iobuf_release(struct partake_iobuf *iobuf) {
    if (iobuf != NULL && --iobuf->management.refcount == 0) {
        partake_iobuf_destroy(iobuf);
    }
}

void partake_iobuf_release_freelist(void);
