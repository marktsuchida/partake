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

#include "partake_daemon.h"
#include "partake_pool.h"
#include "partake_shmem.h"

#include <zf_log.h>


struct daemon {
    const struct partake_daemon_config *config;

    // We currently use a single shared memory segment
    struct partake_shmem_impl* shmem_impl;
    void *shmem_private_data;
    void *addr;

    struct partake_pool *pool;
};


static int setup_shmem(struct daemon *daemon) {
    switch (daemon->config->type) {
        case PARTAKE_SHMEM_MMAP:
            daemon->shmem_impl = partake_shmem_mmap_impl();
            break;
        case PARTAKE_SHMEM_SHMGET:
            daemon->shmem_impl = partake_shmem_shmget_impl();
            break;
        case PARTAKE_SHMEM_WIN32:
            daemon->shmem_impl = partake_shmem_win32_impl();
            break;
        default:
            daemon->shmem_impl = NULL;
            break;
    }
    if (daemon->shmem_impl == NULL) {
        ZF_LOGE("Unknown shared memory type");
        return -1;
    }
    if (daemon->shmem_impl->allocate == NULL) {
        ZF_LOGE("This platform (or partaked build) does not support %s",
                daemon->shmem_impl->name);
        daemon->shmem_impl = NULL;
        return -1;
    }

    int ret = daemon->shmem_impl->initialize(&daemon->shmem_private_data);
    if (ret != 0)
        return ret;

    ret = daemon->shmem_impl->allocate(daemon->config,
            daemon->shmem_private_data);
    if (ret != 0) {
        daemon->shmem_impl->deinitialize(daemon->shmem_private_data);
        return ret;
    }

    daemon->addr = daemon->shmem_impl->getaddr(daemon->shmem_private_data);

    return 0;
}


static void teardown_shmem(struct daemon *daemon) {
    if (daemon->shmem_impl == NULL) {
        return;
    }

    daemon->shmem_impl->deallocate(daemon->config, daemon->shmem_private_data);
    daemon->shmem_impl->deinitialize(daemon->shmem_private_data);
}


static int setup_server(struct daemon *daemon) {
    // TODO Set up event loop and server socket
    return 0;
}


static int shutdown_server(struct daemon *daemon) {
    // TODO Stop server socket and event loop
    return 0;
}


static int run_event_loop(struct daemon *daemon) {
    // TODO
    return 0;
}


int partake_daemon_run(const struct partake_daemon_config *config) {
    struct daemon daemon;
    memset(&daemon, 0, sizeof(daemon));
    daemon.config = config;

    int ret = setup_shmem(&daemon);
    if (ret != 0)
        goto exit;

    daemon.pool = partake_pool_create(daemon.addr, daemon.config->size);
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
    teardown_shmem(&daemon);
    return ret;
}
