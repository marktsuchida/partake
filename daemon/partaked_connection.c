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


struct partake_connection *partake_connection_create(uint32_t conn_no,
        uv_loop_t *loop, struct partake_pool *pool) {
    struct partake_connection *conn = partake_malloc(sizeof(*conn));

    conn->chan = partake_channel_create(pool);

    int err = uv_pipe_init(loop, &conn->client, 0);
    if (err != 0) {
        ZF_LOGE("uv_pipe_init: %s", uv_strerror(err));
        partake_free(conn);
        return NULL;
    }
    conn->client.data = conn;

    conn->readbuf = NULL;
    conn->readbuf_start = 0;

    conn->conn_no = conn_no;

    return conn;
}


static void on_client_close(uv_handle_t *handle) {
    struct partake_connection *conn = handle->data;
    partake_iobuf_decref(conn->readbuf);
    if (!conn->skip_channel_destruction)
        partake_channel_destroy(conn->chan);
    partake_free(conn->name);
    partake_free(conn);
}


void partake_connection_destroy(struct partake_connection *conn) {
    uv_read_stop((uv_stream_t *)&conn->client);
    uv_close((uv_handle_t *)&conn->client, on_client_close);

    struct partake_daemon *daemon = conn->client.loop->data;
    partake_daemon_remove_connection(daemon, conn);
}


static void on_client_shutdown(uv_shutdown_t *shutdownreq, int status) {
    if (status < 0) {
        ZF_LOGE("uv_shutdown_cb: %s", uv_strerror(status));
    }
    struct partake_connection *conn = shutdownreq->data;
    partake_connection_destroy(conn);
}


void partake_connection_shutdown(struct partake_connection *conn) {
    uv_read_stop((uv_stream_t *)&conn->client);
    uv_shutdown_t *shutdownreq = partake_malloc(sizeof(*shutdownreq));
    uv_shutdown(shutdownreq, (uv_stream_t *)&conn->client, on_client_shutdown);
    shutdownreq->data = conn;
}


void partake_connection_alloc_cb(uv_handle_t *client, size_t size,
        uv_buf_t *buf) {
    struct partake_connection *conn = client->data;

    if (conn->readbuf == NULL) {
        conn->readbuf_start = 0;
        conn->readbuf = partake_iobuf_create(PARTAKE_IOBUF_STD_SIZE);
    }

    buf->base = (char *)conn->readbuf->buffer + conn->readbuf_start;
    buf->len = conn->readbuf->capacity - conn->readbuf_start;
}


void partake_connection_read_cb(uv_stream_t *client, ssize_t nread,
        const uv_buf_t *buf) {
    struct partake_connection *conn = client->data;

    if (nread == UV_EOF) { // Client disconnected
        partake_connection_shutdown(conn);
        return;
    }
    if (nread < 0) {
        ZF_LOGE("uv_read_cb: %s", uv_strerror((int)nread));
        ZF_LOGI("Closing connection due to error");
        partake_connection_destroy(conn);
        return;
    }

    struct partake_sender *sender = partake_sender_create(client);

    size_t size = conn->readbuf_start + nread;
    size_t start = 0;
    size_t frame_size;
    bool frame_complete = true;
    bool quit = false;
    while (partake_requestframe_scan(conn->readbuf, start, size,
                &frame_size, &frame_complete)) {
        if (!frame_complete)
            break;

        // TODO Dynamic alloc is superfluous
        struct partake_reqarray *reqarr =
            partake_reqarray_create(conn->readbuf, start);
        if (reqarr == NULL) {
            ZF_LOGI("Dropping connection due to request verification failure");
            quit = true;
            break;
        }

        uint32_t count = partake_reqarray_count(reqarr);
        for (uint32_t i = 0; i < count; ++i) {
            struct partake_request *req = partake_reqarray_request(reqarr, i);
            if (partake_task_handle(conn, req, sender) != 0) {
                quit = true;
                break;
            }
        }

        partake_reqarray_destroy(reqarr);

        if (quit)
            break;

        start += frame_size;
        size -= frame_size;
    }

    partake_sender_flush(sender);
    partake_sender_set_autoflush(sender, true);
    partake_sender_decref(sender);

    if (!frame_complete && !quit) {
        conn->readbuf = partake_requestframe_maybe_move(conn->readbuf,
                &start, size);
        conn->readbuf_start = start + size;
    }
    else {
        partake_iobuf_decref(conn->readbuf);
        conn->readbuf = NULL;
        conn->readbuf_start = 0;
    }

    if (quit) {
        partake_connection_shutdown(conn);
    }
}
