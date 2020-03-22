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

#include "prefix.h"

#include "partake_channel.h"
#include "partake_connection.h"
#include "partake_daemon.h"
#include "partake_iobuf.h"
#include "partake_malloc.h"
#include "partake_protocol_reader.h"
#include "partake_request.h"
#include "partake_response.h"
#include "partake_sender.h"
#include "partake_task.h"

#include <uv.h>
#include <zf_log.h>


struct partake_connection *partake_connection_create(uv_loop_t *loop,
        struct partake_pool *pool) {
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

    return conn;
}


static void on_client_close(uv_handle_t *handle) {
    struct partake_connection *conn = handle->data;
    partake_iobuf_release(conn->readbuf);
    partake_channel_destroy(conn->chan);
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


// 'start' is input-output parameter
static struct partake_iobuf *maybe_shift_partial_message(
        struct partake_iobuf *iobuf, size_t *start, size_t size) {
    // For now we just always copy unless already at start
    if (*start > 0) {
        struct partake_iobuf *newbuf =
            partake_iobuf_create(PARTAKE_IOBUF_STD_SIZE);
        memcpy(newbuf->buffer, (char *)iobuf->buffer + *start, size);
        partake_iobuf_release(iobuf);
        *start = 0;
        return newbuf;
    }
    return iobuf;
}


static bool handle_request(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_protocol_AnyRequest_union_type_t type = partake_request_type(req);

    void (*func)(struct partake_channel *, struct partake_request *,
            struct partake_sender *);
    switch (type) {
        case partake_protocol_AnyRequest_GetSegmentRequest:
            func = partake_task_GetSegment;
            break;

        case partake_protocol_AnyRequest_AllocRequest:
            func = partake_task_Alloc;
            break;

        case partake_protocol_AnyRequest_ReallocRequest:
            func = partake_task_Realloc;
            break;

        case partake_protocol_AnyRequest_OpenRequest:
            func = partake_task_Open;
            break;

        case partake_protocol_AnyRequest_CloseRequest:
            func = partake_task_Close;
            break;

        case partake_protocol_AnyRequest_PublishRequest:
            func = partake_task_Publish;
            break;

        case partake_protocol_AnyRequest_UnpublishRequest:
            func = partake_task_Unpublish;
            break;

        case partake_protocol_AnyRequest_QuitRequest:
            func = partake_task_Quit;
            break;

        default:
            func = partake_task_Unknown;
            break;
    }

    func(conn->chan, req, sender);

    return type != partake_protocol_AnyRequest_QuitRequest;
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
        struct partake_requestmessage *reqmsg =
            partake_requestmessage_create(conn->readbuf, start);

        uint32_t count = partake_requestmessage_count(reqmsg);
        for (uint32_t i = 0; i < count; ++i) {
            quit = !handle_request(conn,
                    partake_requestmessage_request(reqmsg, i), sender);
            if (quit)
                break;
        }

        partake_requestmessage_destroy(reqmsg);

        if (quit)
            break;

        start += frame_size;
        size -= frame_size;
    }

    partake_sender_flush(sender);
    partake_sender_set_autoflush(sender, true);
    partake_sender_release(sender);

    if (!frame_complete && !quit) {
        conn->readbuf = maybe_shift_partial_message(conn->readbuf,
                &start, size);
        conn->readbuf_start = start + size;
    }
    else {
        partake_iobuf_release(conn->readbuf);
        conn->readbuf = NULL;
        conn->readbuf_start = 0;
    }

    if (quit) {
        partake_connection_shutdown(conn);
    }
}
