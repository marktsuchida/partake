/*
 * Connections to partaked
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_channel.h"
#include "partaked_connection.h"
#include "partaked_daemon.h"
#include "partaked_iobuf.h"
#include "partaked_malloc.h"
#include "partaked_request.h"
#include "partaked_response.h"
#include "partaked_sender.h"
#include "partaked_task.h"

#include "partake_protocol_reader.h"

#include <uv.h>
#include <zf_log.h>

struct partaked_connection *
partaked_connection_create(uint32_t conn_no, uv_loop_t *loop,
                           struct partaked_pool *pool) {
    struct partaked_connection *conn = partaked_malloc(sizeof(*conn));

    conn->chan = partaked_channel_create(pool);

    int err = uv_pipe_init(loop, &conn->client, 0);
    if (err != 0) {
        ZF_LOGE("uv_pipe_init: %s", uv_strerror(err));
        partaked_free(conn);
        return NULL;
    }
    conn->client.data = conn;

    conn->readbuf = NULL;
    conn->readbuf_start = 0;

    conn->conn_no = conn_no;

    return conn;
}

static void on_client_close(uv_handle_t *handle) {
    struct partaked_connection *conn = handle->data;
    partaked_iobuf_decref(conn->readbuf);
    if (!conn->skip_channel_destruction)
        partaked_channel_destroy(conn->chan);
    partaked_free(conn->name);
    partaked_free(conn);
}

void partaked_connection_destroy(struct partaked_connection *conn) {
    uv_read_stop((uv_stream_t *)&conn->client);
    uv_close((uv_handle_t *)&conn->client, on_client_close);

    struct partaked_daemon *daemon = conn->client.loop->data;
    partaked_daemon_remove_connection(daemon, conn);
}

static void on_client_shutdown(uv_shutdown_t *shutdownreq, int status) {
    if (status < 0) {
        ZF_LOGE("uv_shutdown_cb: %s", uv_strerror(status));
    }
    struct partaked_connection *conn = shutdownreq->data;
    partaked_connection_destroy(conn);
}

void partaked_connection_shutdown(struct partaked_connection *conn) {
    uv_read_stop((uv_stream_t *)&conn->client);
    uv_shutdown_t *shutdownreq = partaked_malloc(sizeof(*shutdownreq));
    uv_shutdown(shutdownreq, (uv_stream_t *)&conn->client, on_client_shutdown);
    shutdownreq->data = conn;
}

void partaked_connection_alloc_cb(uv_handle_t *client, size_t size,
                                  uv_buf_t *buf) {
    struct partaked_connection *conn = client->data;

    if (conn->readbuf == NULL) {
        conn->readbuf_start = 0;
        conn->readbuf = partaked_iobuf_create(PARTAKED_IOBUF_STD_SIZE);
    }

    buf->base = (char *)conn->readbuf->buffer + conn->readbuf_start;
    buf->len = conn->readbuf->capacity - conn->readbuf_start;
}

void partaked_connection_read_cb(uv_stream_t *client, ssize_t nread,
                                 const uv_buf_t *buf) {
    struct partaked_connection *conn = client->data;

    if (nread == UV_EOF) { // Client disconnected
        partaked_connection_shutdown(conn);
        return;
    }
    if (nread < 0) {
        ZF_LOGE("uv_read_cb: %s", uv_strerror((int)nread));
        ZF_LOGI("Closing connection due to error");
        partaked_connection_destroy(conn);
        return;
    }

    struct partaked_sender *sender = partaked_sender_create(client);

    size_t size = conn->readbuf_start + nread;
    size_t start = 0;
    size_t frame_size;
    bool frame_complete = true;
    bool quit = false;
    while (partaked_requestframe_scan(conn->readbuf, start, size, &frame_size,
                                      &frame_complete)) {
        if (!frame_complete)
            break;

        // TODO Dynamic alloc is superfluous
        struct partaked_reqarray *reqarr =
            partaked_reqarray_create(conn->readbuf, start);
        if (reqarr == NULL) {
            ZF_LOGI("Dropping connection due to request verification failure");
            quit = true;
            break;
        }

        uint32_t count = partaked_reqarray_count(reqarr);
        for (uint32_t i = 0; i < count; ++i) {
            struct partaked_request *req = partaked_reqarray_request(reqarr, i);
            if (partaked_task_handle(conn, req, sender) != 0) {
                quit = true;
                break;
            }
        }

        partaked_reqarray_destroy(reqarr);

        if (quit)
            break;

        start += frame_size;
        size -= frame_size;
    }

    partaked_sender_flush(sender);
    partaked_sender_set_autoflush(sender, true);
    partaked_sender_decref(sender);

    if (!frame_complete && !quit) {
        conn->readbuf =
            partaked_requestframe_maybe_move(conn->readbuf, &start, size);
        conn->readbuf_start = start + size;
    } else {
        partaked_iobuf_decref(conn->readbuf);
        conn->readbuf = NULL;
        conn->readbuf_start = 0;
    }

    if (quit) {
        partaked_connection_shutdown(conn);
    }
}
