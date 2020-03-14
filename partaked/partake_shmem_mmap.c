/*
 * Shared memory from mmap() for partaked
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>


struct mmap_private_data {
    int fd;
    bool fd_open;
    const char *shmname; // non-null indicates shm/file created
    bool must_free_shmname;
    void *addr; // non-null indicates mapped
};


static int mmap_initialize(void **data) {
    *data = malloc(sizeof(struct mmap_private_data));
    if (!*data) {
        return ENOMEM;
    }
    memset(*data, 0, sizeof(struct mmap_private_data));
    return 0;
}


static void mmap_deinitialize(void *data) {
    struct mmap_private_data *d = data;
    if (d == NULL) {
        return;
    }

    if (d->addr != NULL) {
        ZF_LOGF("Deinitializing mmap segment that is still mapped!");
    }
    if (d->fd_open) {
        ZF_LOGF("Deinitializing mmap segment whose fd is still open!");
    }
    if (d->shmname != NULL) {
        ZF_LOGF("Deinitializing mmap segment whose shm/file still exists!");
    }

    free(data);
}


static int create_posix_shm(const struct partake_daemon_config *config,
        struct mmap_private_data *d) {
    bool generate_name = config->shmem.mmap.shmname == NULL;
    bool force = config->force && !generate_name;

    char *generated_name = NULL;
    char *name;

    if (!generate_name) {
        name = config->shmem.mmap.shmname;
    }

    int NUM_RETRIES = 100;
    for (int i = 0; i < NUM_RETRIES; ++i) {
        if (generate_name) {
            name = generated_name = partake_alloc_random_name("/", 32);
        }

        errno = 0;
        d->fd = shm_open(name, O_RDWR | O_CREAT | (force ? 0 : O_EXCL), 0666);
        int ret = errno;
        if (d->fd < 0) {
            char emsg[1024];
            ZF_LOGE("shm_open: %s: %s", name,
                    partake_strerror(ret, emsg, sizeof(emsg)));
            free(generated_name);

            if (generate_name && ret == EEXIST) {
                continue;
            }
            return ret;
        }

        ZF_LOGI("shm_open: %s: fd = %d", name, d->fd);

        d->shmname = name;
        d->must_free_shmname = generate_name;
        d->fd_open = true;
        return 0;
    }

    ZF_LOGE("Giving up after trying %d names", NUM_RETRIES);
    return EEXIST;
}


static int unlink_posix_shm(struct mmap_private_data *d) {
    if (d->shmname == NULL) {
        return 0;
    }

    int ret = 0;
    errno = 0;
    if (shm_unlink(d->shmname) != 0) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("shm_unlink: %s: %s", d->shmname,
                partake_strerror(ret, emsg, sizeof(emsg)));
    }

    // Marked unlinked even on error; there is nothing further we can do.
    if (d->must_free_shmname) {
        free((void *)d->shmname);
    }
    d->shmname = NULL;

    return ret;
}


static int create_file_shm(const struct partake_daemon_config *config,
        struct mmap_private_data *d) {
    if (d->must_free_shmname) {
        free((void *)d->shmname);
    }
    errno = 0;
    d->shmname = realpath(config->shmem.mmap.filename, NULL);
    d->must_free_shmname = true;
    int ret = errno;
    if (d->shmname == NULL) {
        char emsg[1024];
        ZF_LOGE("realpath: %s: %s",
                config->shmem.mmap.filename,
                partake_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    errno = 0;
    d->fd = open(d->shmname,
            O_RDWR | O_CREAT | (config->force ? 0 : O_EXCL) | O_CLOEXEC,
            0666);
    ret = errno;
    if (d->fd < 0) {
        char emsg[1024];
        ZF_LOGE("open: %s: %s", d->shmname,
                partake_strerror(ret, emsg, sizeof(emsg)));
        if (d->must_free_shmname) {
            free((void *)d->shmname);
        }
        d->shmname = NULL;
        return ret;
    }
    ZF_LOGI("open: %s: fd = %d", d->shmname, d->fd);

    d->fd_open = true;
    return 0;
}


static int unlink_file_shm(struct mmap_private_data *d) {
    if (d->shmname == NULL) {
        return 0;
    }

    int ret = 0;
    errno = 0;
    if (unlink(d->shmname) != 0) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("unlink: %s: %s", d->shmname,
                partake_strerror(ret, emsg, sizeof(emsg)));
    }

    // Marked unlinked even on error; there is nothing further we can do.
    if (d->must_free_shmname) {
        free((void *)d->shmname);
    }
    d->shmname = NULL;

    return ret;
}


static int close_fd(struct mmap_private_data *d) {
    if (!d->fd_open) {
        return 0;
    }

    errno = 0;
    if (close(d->fd) != 0) {
        int ret = errno;
        char emsg[1024];
        ZF_LOGE("close: %d: %s", d->fd,
                partake_strerror(ret, emsg, sizeof(emsg)));
    }
    else {
        ZF_LOGI("close: %d", d->fd);
    }

    // We mark the fd closed even if close() fails, because repeating a close()
    // is a bug (see e.g. Linux man page for close(2)).
    d->fd_open = false;
    return 0;
}


static int mmap_allocate(const struct partake_daemon_config *config,
        void *data) {
    struct mmap_private_data *d = data;

    int ret = config->shmem.mmap.shm_open ?
        create_posix_shm(config, d) :
        create_file_shm(config, d);
    if (ret != 0)
        return ret;

    errno = 0;
    if (ftruncate(d->fd, config->size) != 0) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("ftruncate: fd %d, %zu bytes: %s", d->fd, config->size,
                partake_strerror(ret, emsg, sizeof(emsg)));
        goto error;
    }

    if (config->size > 0) {
        errno = 0;
        d->addr = mmap(NULL, config->size, PROT_READ | PROT_WRITE,
                MAP_SHARED, d->fd, 0);
        if (d->addr == NULL) {
            ret = errno;
            char emsg[1024];
            ZF_LOGE("mmap: fd %d: %s", d->fd,
                    partake_strerror(ret, emsg, sizeof(emsg)));
            goto error;
        }
        ZF_LOGI("mmap: fd %d: %zu bytes at %p", d->fd, config->size, d->addr);
    }
    else {
        ZF_LOGI("fd %d: mmap skipped due to zero size", d->fd);
    }

    close_fd(d);
    return 0;

error:
    close_fd(d);
    config->shmem.mmap.shm_open ?  unlink_posix_shm(d) : unlink_file_shm(d);
    return ret;
}


static void mmap_deallocate(const struct partake_daemon_config *config,
        void *data) {
    struct mmap_private_data *d = data;

    // fd is already closed in allocate()

    config->shmem.mmap.shm_open ?
        unlink_posix_shm(d) :
        unlink_file_shm(d);

    if (d->addr != NULL) {
        errno = 0;
        if (munmap(d->addr, config->size) != 0) {
            int ret = errno;
            char emsg[1024];
            ZF_LOGE("munmap: %zu bytes at %p: %s", config->size, d->addr,
                    partake_strerror(ret, emsg, sizeof(emsg)));
        }
        else {
            ZF_LOGI("munmap: %zu bytes at %p", config->size, d->addr);
        }
    }
}


static void *mmap_getaddr(void *data) {
    struct mmap_private_data *d = data;
    return d->addr;
}

#endif // _WIN32


static struct partake_shmem_impl mmap_impl = {
    .name = "mmap-based shared memory",
#ifndef _WIN32
    .initialize = mmap_initialize,
    .deinitialize = mmap_deinitialize,
    .allocate = mmap_allocate,
    .deallocate = mmap_deallocate,
    .getaddr = mmap_getaddr,
#endif
};


struct partake_shmem_impl *partake_shmem_mmap_impl(void) {
    return &mmap_impl;
}
