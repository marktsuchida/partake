/*
 * Connections to partaked
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <utlist.h>
#include <uv.h>

#include <stdbool.h>
#include <stdint.h>

struct partake_pool;


struct partake_connection {
    struct partake_channel *chan;

    uv_pipe_t client;

    // The buffer currently being read into, if any.
    struct partake_iobuf *readbuf;
    size_t readbuf_start;

    bool has_said_hello;

    uint32_t conn_no;
    uint32_t pid;
    char *name; // UTF-8

    // We keep a list of connections so that we can close them upon server
    // shutdown.
    struct partake_connection *prev;
    struct partake_connection *next;

    // Used to skip extra work when the server is shutting down anyway.
    bool skip_channel_destruction;
};


struct partake_connection *partake_connection_create(uint32_t conn_no,
        uv_loop_t *loop, struct partake_pool *pool);

void partake_connection_destroy(struct partake_connection *conn);

// Like destroy, but waits for pending writes to complete.
void partake_connection_shutdown(struct partake_connection *conn);


void partake_connection_alloc_cb(uv_handle_t *client, size_t size,
        uv_buf_t *buf);

void partake_connection_read_cb(uv_stream_t *client, ssize_t nread,
        const uv_buf_t *buf);
