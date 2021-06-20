/*
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_connection.h"
#include "partaked_daemon.h"
#include "partaked_logging.h"
#include "partaked_malloc.h"
#include "partaked_pool.h"
#include "partaked_segment.h"
#include "partaked_tchar.h"

#include <utlist.h>
#include <uv.h>
#include <zf_log.h>


struct partaked_daemon {
    const struct partaked_daemon_config *config;

    // We currently use a single shared memory segment
    struct partaked_segment *segment;

    struct partaked_pool *pool;

    uv_loop_t loop;

    uv_pipe_t server;

    uv_signal_t signal[3]; // INT, HUP, TERM

    // All connections (created and not yet removed)
    struct partaked_connection *conns; // doubly-linked list

    uint32_t last_conn_no;
};


static void on_connect(uv_stream_t *server, int status) {
    if (status < 0) {
        ZF_LOGE("uv_connection_cb: %s", uv_strerror(status));
        return;
    }

    struct partaked_daemon *daemon = server->loop->data;

    struct partaked_connection *conn = partaked_connection_create(
            ++daemon->last_conn_no, server->loop, daemon->pool);
    if (conn == NULL) {
        return;
    }
    DL_PREPEND(daemon->conns, conn);

    int err = uv_accept((uv_stream_t *)server, (uv_stream_t *)&conn->client);
    if (err != 0) {
        ZF_LOGE("uv_accept: %s", uv_strerror(err));
        partaked_connection_destroy(conn);
        return;
    }

    err = uv_read_start((uv_stream_t *)&conn->client,
            partaked_connection_alloc_cb, partaked_connection_read_cb);
    if (err != 0) {
        ZF_LOGE("uv_read_start: %s", uv_strerror(err));
        partaked_connection_destroy(conn);
        return;
    }
}


void partaked_daemon_remove_connection(struct partaked_daemon *daemon,
        struct partaked_connection *conn) {
    DL_DELETE(daemon->conns, conn);
}


static void on_server_close(uv_handle_t *handle) {
    struct partaked_daemon *daemon = handle->loop->data;

    struct partaked_connection *conn, *tmp;
    DL_FOREACH_SAFE(daemon->conns, conn, tmp) {
        // Skipping channel is not only faster, but also avoids firing pending
        // unpublish tasks (although they would probably be harmless if they
        // do).
        conn->skip_channel_destruction = true;
        partaked_connection_destroy(conn);
    }
}


static void on_signal(uv_signal_t *handle, int signum) {
    ZF_LOGI("Signal %d received", signum);

    struct partaked_daemon *daemon = handle->loop->data;

    for (int i = 0; i < 3; ++i) {
        uv_signal_stop(&daemon->signal[i]);
        uv_close((uv_handle_t *)&daemon->signal[i], NULL);
    }

    uv_close((uv_handle_t *)&daemon->server, on_server_close);
}


static int setup_server(struct partaked_daemon *daemon) {
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
            partaked_tstrtoutf8(daemon->config->socket, name, sizeof(name)));
    if (err != 0) {
        ZF_LOGE("uv_pipe_bind: %s", uv_strerror(err));
        goto error;
    }

    err = uv_listen((uv_stream_t *)&daemon->server, 32, on_connect);
    if (err != 0) {
        ZF_LOGE("uv_listen: %s", uv_strerror(err));
        goto error;
    }
    TCHAR buf[1024];
    ZF_LOGI("Listening on %s",
            partaked_strtolog(daemon->config->socket, buf, sizeof(buf)));

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


static int shutdown_server(struct partaked_daemon *daemon) {
    int err = uv_loop_close(&daemon->loop);
    if (err != 0) {
        ZF_LOGE("uv_loop_close: %s", uv_strerror(err));
        return err;
    }

    return 0;
}


static int run_event_loop(struct partaked_daemon *daemon) {
    ZF_LOGI("Calling uv_run");
    int err = uv_run(&daemon->loop, UV_RUN_DEFAULT);
    ZF_LOGI("uv_run exited");

    return err;
}


int partaked_daemon_run(const struct partaked_daemon_config *config) {
    partaked_initialize_malloc();
    uv_replace_allocator(partaked_malloc, partaked_realloc, partaked_calloc,
            (void (*)(void *))partaked_free);

    struct partaked_daemon daemon;
    memset(&daemon, 0, sizeof(daemon));
    daemon.config = config;

    int ret = 0;
    daemon.segment = partaked_segment_create(daemon.config);
    if (daemon.segment == NULL) {
        ret = -1;
        goto exit;
    }

    daemon.pool = partaked_pool_create(&daemon.loop, daemon.segment);
    if (daemon.pool == NULL) {
        ret = -1;
        goto exit;
    }

    ret = setup_server(&daemon);
    if (ret != 0)
        goto exit;

    ret = run_event_loop(&daemon);

    shutdown_server(&daemon);

exit:
    partaked_pool_destroy(daemon.pool);
    partaked_segment_destroy(daemon.segment);
    return ret;
}
