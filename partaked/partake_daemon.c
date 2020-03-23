/*
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

#include "partake_connection.h"
#include "partake_daemon.h"
#include "partake_malloc.h"
#include "partake_pool.h"
#include "partake_segment.h"

#include <utlist.h>
#include <uv.h>
#include <zf_log.h>


struct partake_daemon {
    const struct partake_daemon_config *config;

    // We currently use a single shared memory segment
    struct partake_segment *segment;

    struct partake_pool *pool;

    uv_loop_t loop;

    uv_pipe_t server;

    uv_signal_t signal[3]; // INT, HUP, TERM

    // All connections (created and note yet removed)
    struct partake_connection *conns; // doubly-linked list

    uint32_t last_conn_no;
};


static void on_connect(uv_stream_t *server, int status) {
    if (status < 0) {
        ZF_LOGE("uv_connection_cb: %s", uv_strerror(status));
        return;
    }

    struct partake_daemon *daemon = server->loop->data;

    struct partake_connection *conn = partake_connection_create(
            ++daemon->last_conn_no, server->loop, daemon->pool);
    if (conn == NULL) {
        return;
    }
    DL_PREPEND(daemon->conns, conn);

    int err = uv_accept((uv_stream_t *)server, (uv_stream_t *)&conn->client);
    if (err != 0) {
        ZF_LOGE("uv_accept: %s", uv_strerror(err));
        partake_connection_destroy(conn);
        return;
    }

    err = uv_read_start((uv_stream_t *)&conn->client,
            partake_connection_alloc_cb, partake_connection_read_cb);
    if (err != 0) {
        ZF_LOGE("uv_read_start: %s", uv_strerror(err));
        partake_connection_destroy(conn);
        return;
    }
}


void partake_daemon_remove_connection(struct partake_daemon *daemon,
        struct partake_connection *conn) {
    DL_DELETE(daemon->conns, conn);
}


static void on_server_close(uv_handle_t *handle) {
    struct partake_daemon *daemon = handle->loop->data;

    struct partake_connection *conn, *tmp;
    DL_FOREACH_SAFE(daemon->conns, conn, tmp) {
        // Skipping channel is not only faster, but also avoids firing pending
        // unpublish tasks (although they would probably be harmless if they
        // do).
        conn->skip_channel_destruction = true;
        partake_connection_destroy(conn);
    }
}


static void on_signal(uv_signal_t *handle, int signum) {
    ZF_LOGI("Signal %d received", signum);

    struct partake_daemon *daemon = handle->loop->data;

    for (int i = 0; i < 3; ++i) {
        uv_signal_stop(&daemon->signal[i]);
        uv_close((uv_handle_t *)&daemon->signal[i], NULL);
    }

    uv_close((uv_handle_t *)&daemon->server, on_server_close);
}


static int setup_server(struct partake_daemon *daemon) {
    int err = uv_loop_init(&daemon->loop);
    if (err != 0) {
        ZF_LOGE("uv_loop_init: %s", uv_strerror(err));
        return err;
    }
    daemon->loop.data = daemon;

    err = uv_pipe_init(&daemon->loop, &daemon->server, 0);
    if (err != 0) {
        ZF_LOGE("uv_pipe_init: %s", uv_strerror(err));
        return err;
    }

    uv_pipe_pending_instances(&daemon->server, 8);

    char name[1024];
    err = uv_pipe_bind(&daemon->server,
            partake_tstrtoutf8(daemon->config->socket, name, sizeof(name)));
    if (err != 0) {
        ZF_LOGE("uv_pipe_bind: %s", uv_strerror(err));
        goto error;
    }

    err = uv_listen((uv_stream_t *)&daemon->server, 32, on_connect);
    if (err != 0) {
        ZF_LOGE("uv_listen: %s", uv_strerror(err));
        goto error;
    }

    int signums[3] = { SIGINT, SIGHUP, SIGTERM };
    for (int i = 0; i < 3; ++i) {
        err = uv_signal_init(&daemon->loop, &daemon->signal[i]);
        if (err != 0) {
            ZF_LOGE("uv_signal_init: %s", uv_strerror(err));
            goto error;
        }

        err = uv_signal_start(&daemon->signal[i], on_signal, signums[i]);
        if (err != 0) {
            ZF_LOGE("uv_signal_start: %s", uv_strerror(err));
            goto error;
        }
    }

    return 0;

error:
    uv_close((uv_handle_t *)&daemon->server, NULL);
    return err;
}


static int shutdown_server(struct partake_daemon *daemon) {
    int err = uv_loop_close(&daemon->loop);
    if (err != 0) {
        ZF_LOGE("uv_loop_close: %s", uv_strerror(err));
        return err;
    }

    return 0;
}


static int run_event_loop(struct partake_daemon *daemon) {
    ZF_LOGI("Calling uv_run");
    int err = uv_run(&daemon->loop, UV_RUN_DEFAULT);
    ZF_LOGI("uv_run exited");

    return 0;
}


int partake_daemon_run(const struct partake_daemon_config *config) {
    partake_initialize_malloc();
    uv_replace_allocator(partake_malloc, partake_realloc, partake_calloc,
            (void (*)(void *))partake_free);

    struct partake_daemon daemon;
    memset(&daemon, 0, sizeof(daemon));
    daemon.config = config;

    int ret = 0;
    daemon.segment = partake_segment_create(daemon.config);
    if (daemon.segment == NULL) {
        ret = -1;
        goto exit;
    }

    daemon.pool = partake_pool_create(daemon.segment);
    if (daemon.pool == NULL) {
        ret = -1;
        goto exit;
    }

    ret = setup_server(&daemon);
    if (ret != 0)
        goto exit;

    ret = run_event_loop(&daemon);

exit:
    shutdown_server(&daemon);
    partake_pool_destroy(daemon.pool);
    partake_segment_destroy(daemon.segment);
    return ret;
}
