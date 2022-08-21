/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <utlist.h>
#include <uv.h>

#include <stdbool.h>
#include <stdint.h>

struct partaked_pool;

struct partaked_connection {
    struct partaked_channel *chan;

    uv_pipe_t client;

    // The buffer currently being read into, if any.
    struct partaked_iobuf *readbuf;
    size_t readbuf_start;

    bool has_said_hello;

    uint32_t conn_no;
    uint32_t pid;
    char *name; // UTF-8

    // We keep a list of connections so that we can close them upon server
    // shutdown.
    struct partaked_connection *prev;
    struct partaked_connection *next;

    // Used to skip extra work when the server is shutting down anyway.
    bool skip_channel_destruction;
};

struct partaked_connection *
partaked_connection_create(uint32_t conn_no, uv_loop_t *loop,
                           struct partaked_pool *pool);

void partaked_connection_destroy(struct partaked_connection *conn);

// Like destroy, but waits for pending writes to complete.
void partaked_connection_shutdown(struct partaked_connection *conn);

void partaked_connection_alloc_cb(uv_handle_t *client, size_t size,
                                  uv_buf_t *buf);

void partaked_connection_read_cb(uv_stream_t *client, ssize_t nread,
                                 const uv_buf_t *buf);
