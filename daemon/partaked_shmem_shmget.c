/*
 * Shared memory from shmget() for partaked
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"
#include "partaked_shmem.h"

#include "partaked_logging.h"
#include "partaked_malloc.h"
#include "partaked_random.h"

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
    *data = partaked_calloc(1, sizeof(struct shmget_private_data));
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

    partaked_free(data);
}

static int create_sysv_shm(const struct partaked_daemon_config *config,
                           struct shmget_private_data *d) {
    if (sizeof(key_t) < sizeof(config->shmem.shmget.key)) {
        // Hopefully this check is pedantic.
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
                d->key = partaked_generate_random_int();
            }
        }

        if (config->size > 0) {
            errno = 0;
            d->shmid = shmget(
                d->key, config->size,
                IPC_CREAT | (force ? 0 : IPC_EXCL) |
#ifdef __linux__
                    // TODO We can also support different huge page sizes here
                    (config->shmem.shmget.huge_pages ? SHM_HUGETLB : 0) |
#endif
                    0666);
            if (d->shmid == -1) {
                int ret = errno;
                char emsg[1024];
                ZF_LOGE("shmget: key %d: %s", d->key,
                        partaked_strerror(ret, emsg, sizeof(emsg)));

                if (generate_key && ret == EEXIST) {
                    continue;
                }
                return ret;
            }
            ZF_LOGI("shmget: key %d: id = %d", d->key, d->shmid);
        } else {
            ZF_LOGI("key %d: shmget skipped due to zero size", d->key);
        }

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

    if (d->addr != NULL) {
        errno = 0;
        if (shmctl(d->shmid, IPC_RMID, NULL) != 0) {
            int ret = errno;
            char emsg[1024];
            ZF_LOGE("shmctl: IPC_RMID id %d: %s", d->shmid,
                    partaked_strerror(ret, emsg, sizeof(emsg)));
        } else {
            ZF_LOGI("shmctl IPC_RMID: id %d", d->shmid);
        }
    }

    d->key_active = false;
    return 0;
}

static int attach_sysv_shm(const struct partaked_daemon_config *config,
                           struct shmget_private_data *d) {
    if (config->size > 0) {
        errno = 0;
        d->addr = shmat(d->shmid, NULL, 0);
        if (d->addr == NULL) {
            int ret = errno;
            char emsg[1024];
            ZF_LOGE("shmat: id %d: %s", d->shmid,
                    partaked_strerror(ret, emsg, sizeof(emsg)));
            return ret;
        }
        ZF_LOGI("shmat: id %d: addr = %p", d->shmid, d->addr);
    } else {
        ZF_LOGI("key %d: shmat skipped due to zero size", d->key);
    }

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
                partaked_strerror(ret, emsg, sizeof(emsg)));
    } else {
        ZF_LOGI("shmdt: %p", d->addr);
    }

    d->addr = NULL;
    return 0;
}

static int shmget_allocate(const struct partaked_daemon_config *config,
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

static void shmget_deallocate(const struct partaked_daemon_config *config,
                              void *data) {
    struct shmget_private_data *d = data;

    detach_sysv_shm(d);
    remove_sysv_shm(d);
}

static void *shmget_getaddr(void *data) {
    struct shmget_private_data *d = data;
    return d->addr;
}

static void shmget_add_mapping_spec(flatcc_builder_t *b, void *data) {
    struct shmget_private_data *d = data;

    partake_protocol_SegmentSpec_spec_SystemVSharedMemorySpec_start(b);
    partake_protocol_SystemVSharedMemorySpec_key_add(b, d->key);
    partake_protocol_SegmentSpec_spec_SystemVSharedMemorySpec_end(b);
}

#endif // _WIN32

static struct partaked_shmem_impl shmget_impl = {
    .name = "shmget-based shared memory",
#ifndef _WIN32
    .initialize = shmget_initialize,
    .deinitialize = shmget_deinitialize,
    .allocate = shmget_allocate,
    .deallocate = shmget_deallocate,
    .getaddr = shmget_getaddr,
    .add_mapping_spec = shmget_add_mapping_spec,
#endif
};

struct partaked_shmem_impl *partaked_shmem_shmget_impl(void) {
    return &shmget_impl;
}
