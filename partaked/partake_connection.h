/*
 * Connections to partaked
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

#include <uthash.h>
#include <uv.h>

struct partake_pool;


struct partake_connection {
    struct partake_channel *chan;

    uv_pipe_t client;

    // The buffer currently being read into, if any.
    struct partake_iobuf *readbuf;
    size_t readbuf_start;

    UT_hash_handle hh; // Key == ptr to this struct
};


struct partake_connection *partake_connection_create(uv_loop_t *loop,
        struct partake_pool *pool);

void partake_connection_destroy(struct partake_connection *conn);

// Like destroy, but waits for pending writes to complete.
void partake_connection_shutdown(struct partake_connection *conn);


void partake_connection_alloc_cb(uv_handle_t *client, size_t size,
        uv_buf_t *buf);

void partake_connection_read_cb(uv_stream_t *client, ssize_t nread,
        const uv_buf_t *buf);
