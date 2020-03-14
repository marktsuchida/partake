/*
 * Shared memory from shmget() for partaked
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

#include "partake_shmem.h"
#include "partake_logging.h"

#ifndef _WIN32

#include <zf_log.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>


struct shmget_private_data {
    key_t key;
    bool key_active;
    int shmid;
    void *addr;
};


static int shmget_initialize(void **data) {
    *data = malloc(sizeof(struct shmget_private_data));
    if (!*data) {
        return ENOMEM;
    }
    memset(*data, 0, sizeof(struct shmget_private_data));
    return 0;
}


static void shmget_deinitialize(void *data) {
    struct shmget_private_data *d = data;
    if (d == NULL) {
        return;
    }

    if (d->addr != NULL) {
        ZF_LOGF("Deinitializing shmget segment that is still attached!");
    }
    if (d->key_active) {
        ZF_LOGF("Deinitializing shmget segment whose key still exists!");
    }

    free(data);
}


static int create_sysv_shm(const struct partake_daemon_config *config,
        struct shmget_private_data *d) {
    if (sizeof(key_t) == sizeof(config->shmem.shmget.key)) { // pedantic check
        ZF_LOGF("Assumption violated for System V IPC key size");
        abort();
    }

    d->key = config->shmem.shmget.key;
    bool generate_key = d->key == 0;
    if (!generate_key && d->key == IPC_PRIVATE) {
        ZF_LOGE("Requested System V IPC key %d equals IPC_PRIVATE", d->key);
        return EINVAL;
    }
    bool force = config->force && !generate_key;

    int NUM_RETRIES = 100;
    for (int i = 0; i < NUM_RETRIES; ++i) {
        if (generate_key) {
            while (d->key == 0 || d->key == IPC_PRIVATE) {
                d->key = partake_generate_random_int();
            }
        }

        errno = 0;
        d->shmid = shmget(d->key, config->size,
                IPC_CREAT |
                (force ? 0 : IPC_EXCL) |
#ifdef __linux__
                // TODO We can also support different huge page sizes here
                (config->shmem.shmget.huge_pages ? SHM_HUGETLB : 0) |
#endif
                0666);
        if (d->shmid == -1) {
            int ret = errno;
            char emsg[1024];
            ZF_LOGE("shmget: key %d: %s", d->key,
                    partake_strerror(ret, emsg, sizeof(emsg)));

            if (generate_key && ret == EEXIST) {
                continue;
            }
            return ret;
        }

        ZF_LOGI("shmget: key %d: id = %d", d->key, d->shmid);

        d->key_active = true;
        return 0;
    }

    ZF_LOGE("Giving up after trying %d keys", NUM_RETRIES);
    return EEXIST;
}


static int remove_sysv_shm(struct shmget_private_data *d) {
    if (!d->key_active) {
        return 0;
    }

    errno = 0;
    if (shmctl(d->shmid, IPC_RMID, NULL) != 0) {
        int ret = errno;
        char emsg[1024];
        ZF_LOGE("shmctl: IPC_RMID id %d: %s", d->shmid,
                partake_strerror(ret, emsg, sizeof(emsg)));
    }
    else {
        ZF_LOGI("shmctl IPC_RMID: id %d", d->shmid);
    }

    d->key_active = false;
    return 0;
}


static int attach_sysv_shm(const struct partake_daemon_config *config,
        struct shmget_private_data *d) {
    errno = 0;
    d->addr = shmat(d->shmid, NULL, 0);
    if (d->addr == NULL) {
        int ret = errno;
        char emsg[1024];
        ZF_LOGE("shmat: id %d: %s", d->shmid,
                partake_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    ZF_LOGI("shmat: id %d: addr = %p", d->shmid, d->addr);
    return 0;
}


static int detach_sysv_shm(struct shmget_private_data *d) {
    if (d->addr == NULL) {
        return 0;
    }

    if (shmdt(d->addr) != 0) {
        int ret = errno;
        char emsg[1024];
        ZF_LOGE("shmdt: %p: %s", d->addr,
                partake_strerror(ret, emsg, sizeof(emsg)));
    }
    else {
        ZF_LOGI("shmdt: %p", d->addr);
    }

    d->addr = NULL;
    return 0;
}


static int shmget_allocate(const struct partake_daemon_config *config,
        void *data) {
    struct shmget_private_data *d = data;

    int ret = create_sysv_shm(config, d);
    if (ret != 0) {
        return ret;
    }

    ret = attach_sysv_shm(config, d);
    if (ret != 0) {
        remove_sysv_shm(d);
        return ret;
    }

    return 0;
}


static void shmget_deallocate(const struct partake_daemon_config *config,
        void *data) {
    struct shmget_private_data *d = data;

    detach_sysv_shm(d);
    remove_sysv_shm(d);
}


static void *shmget_getaddr(void *data) {
    struct shmget_private_data *d = data;
    return d->addr;
}

#endif // _WIN32


static struct partake_shmem_impl shmget_impl = {
    .name = "shmget-based shared memory",
#ifndef _WIN32
    .initialize = shmget_initialize,
    .deinitialize = shmget_deinitialize,
    .allocate = shmget_allocate,
    .deallocate = shmget_deallocate,
    .getaddr = shmget_getaddr,
#endif
};


struct partake_shmem_impl *partake_shmem_shmget_impl(void) {
    return &shmget_impl;
}
