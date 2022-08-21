/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
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
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// Limit generated name to platform maximum.
#if defined(__APPLE__)
#include <sys/posix_shm.h>
#define MAX_SHM_OPEN_NAME_LEN (PSHMNAMLEN > 255 ? 255 : PSHMNAMLEN)
#else
#define MAX_SHM_OPEN_NAME_LEN 255
#endif

struct mmap_private_data {
    bool use_posix; // Or else filesystem
    int fd;
    bool fd_open;
    const char *shmname; // non-null indicates shm/file created
    bool must_free_shmname;
    void *addr; // non-null indicates mapped
};

static int mmap_initialize(void **data) {
    *data = partaked_malloc(sizeof(struct mmap_private_data));
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

    partaked_free(data);
}

static int create_posix_shm(const struct partaked_daemon_config *config,
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
            name = generated_name =
                partaked_alloc_random_name("/", 32, MAX_SHM_OPEN_NAME_LEN);
        }

        errno = 0;
        d->fd = shm_open(name, O_RDWR | O_CREAT | (force ? 0 : O_EXCL), 0666);
        int ret = errno;
        if (d->fd < 0) {
            char emsg[1024];
            ZF_LOGE("shm_open: %s: %s", name,
                    partaked_strerror(ret, emsg, sizeof(emsg)));
            partaked_free(generated_name);

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
                partaked_strerror(ret, emsg, sizeof(emsg)));
    }
    ZF_LOGI("shm_unlink: %s", d->shmname);

    // Marked unlinked even on error; there is nothing further we can do.
    if (d->must_free_shmname) {
        partaked_free((void *)d->shmname);
    }
    d->shmname = NULL;

    return ret;
}

// On return, *canonical is set to either 'name' or an allocated string.
static int canonicalize_filename(const char *name, const char **canonical) {
    if (strcmp(name, "/") == 0) { // Get edge case out of the way
        *canonical = name;
        return 0;
    }

    int ret = 0;
    char *name1 = NULL, *name2 = NULL, *rdname = NULL;

    // We need to run realpath() on the directory, since the file may not
    // exist yet. Some implementations of dirname() and basename() can modify
    // the given path, so we need to make copies.

    name1 = partaked_malloc(strlen(name) + 1);
    strcpy(name1, name);
    errno = 0;
    char *dirnm = dirname(name1);
    if (dirnm == NULL) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("dirname: %s: %s", name,
                partaked_strerror(ret, emsg, sizeof(emsg)));
        goto exit;
    }

    name2 = partaked_malloc(strlen(name) + 1);
    strcpy(name2, name);
    errno = 0;
    char *basnm = basename(name2);
    if (basnm == NULL) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("basename: %s: %s", name,
                partaked_strerror(ret, emsg, sizeof(emsg)));
        goto exit;
    }

    rdname = partaked_malloc(PATH_MAX + 1);
    errno = 0;
    char *realdirnm = realpath(dirnm, rdname);
    if (realdirnm == NULL) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("realpath: %s: %s", dirnm,
                partaked_strerror(ret, emsg, sizeof(emsg)));
        goto exit;
    }

    *canonical = partaked_malloc(strlen(realdirnm) + 1 + strlen(basnm) + 1);
    char *c = (char *)*canonical;
    strcpy(c, realdirnm);
    strcat(c, "/");
    strcat(c, basnm);

exit:
    partaked_free(rdname);
    partaked_free(name2);
    partaked_free(name1);
    return ret;
}

static int create_file_shm(const struct partaked_daemon_config *config,
                           struct mmap_private_data *d) {
    if (d->must_free_shmname) {
        partaked_free((void *)d->shmname);
    }

    // We need to use a canonicalized path, because it will be passed to
    // clients for them to also open.
    int ret = canonicalize_filename(config->shmem.mmap.filename, &d->shmname);
    if (ret != 0)
        return ret;
    d->must_free_shmname = d->shmname != config->shmem.mmap.filename;

    errno = 0;
    d->fd = open(d->shmname,
                 O_RDWR | O_CREAT | (config->force ? 0 : O_EXCL) | O_CLOEXEC,
                 0666);
    ret = errno;
    if (d->fd < 0) {
        char emsg[1024];
        ZF_LOGE("open: %s: %s", d->shmname,
                partaked_strerror(ret, emsg, sizeof(emsg)));
        if (d->must_free_shmname) {
            partaked_free((void *)d->shmname);
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
                partaked_strerror(ret, emsg, sizeof(emsg)));
    }
    ZF_LOGI("unlink: %s", d->shmname);

    // Marked unlinked even on error; there is nothing further we can do.
    if (d->must_free_shmname) {
        partaked_free((void *)d->shmname);
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
                partaked_strerror(ret, emsg, sizeof(emsg)));
    } else {
        ZF_LOGI("close: %d", d->fd);
    }

    // We mark the fd closed even if close() fails, because repeating a close()
    // is a bug (see e.g. Linux man page for close(2)).
    d->fd_open = false;
    return 0;
}

static int mmap_allocate(const struct partaked_daemon_config *config,
                         void *data) {
    struct mmap_private_data *d = data;

    d->use_posix = config->shmem.mmap.shm_open;

    int ret = d->use_posix ? create_posix_shm(config, d)
                           : create_file_shm(config, d);
    if (ret != 0)
        return ret;

    errno = 0;
    if (ftruncate(d->fd, config->size) != 0) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("ftruncate: fd %d, %zu bytes: %s", d->fd, config->size,
                partaked_strerror(ret, emsg, sizeof(emsg)));
        goto error;
    }
    ZF_LOGI("ftruncate: fd %d, %zu bytes", d->fd, config->size);

    if (config->size > 0) {
        errno = 0;
        d->addr = mmap(NULL, config->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       d->fd, 0);
        if (d->addr == NULL) {
            ret = errno;
            char emsg[1024];
            ZF_LOGE("mmap: fd %d: %s", d->fd,
                    partaked_strerror(ret, emsg, sizeof(emsg)));
            goto error;
        }
        ZF_LOGI("mmap: fd %d: %zu bytes at %p", d->fd, config->size, d->addr);
    } else {
        ZF_LOGI("fd %d: mmap skipped due to zero size", d->fd);
    }

    close_fd(d);
    return 0;

error:
    close_fd(d);
    config->shmem.mmap.shm_open ? unlink_posix_shm(d) : unlink_file_shm(d);
    return ret;
}

static void mmap_deallocate(const struct partaked_daemon_config *config,
                            void *data) {
    struct mmap_private_data *d = data;

    // fd is already closed in allocate()

    config->shmem.mmap.shm_open ? unlink_posix_shm(d) : unlink_file_shm(d);

    if (d->addr != NULL) {
        errno = 0;
        if (munmap(d->addr, config->size) != 0) {
            int ret = errno;
            char emsg[1024];
            ZF_LOGE("munmap: %zu bytes at %p: %s", config->size, d->addr,
                    partaked_strerror(ret, emsg, sizeof(emsg)));
        } else {
            ZF_LOGI("munmap: %zu bytes at %p", config->size, d->addr);
        }
        d->addr = NULL;
    }
}

static void *mmap_getaddr(void *data) {
    struct mmap_private_data *d = data;
    return d->addr;
}

static void mmap_add_mapping_spec(flatcc_builder_t *b, void *data) {
    struct mmap_private_data *d = data;

    partake_protocol_SegmentSpec_spec_PosixMmapSpec_start(b);
    partake_protocol_PosixMmapSpec_name_create_str(b, d->shmname);
    partake_protocol_PosixMmapSpec_use_shm_open_add(b, d->use_posix);
    partake_protocol_SegmentSpec_spec_PosixMmapSpec_end(b);
}

#endif // _WIN32

static struct partaked_shmem_impl mmap_impl = {
    .name = "mmap-based shared memory",
#ifndef _WIN32
    .initialize = mmap_initialize,
    .deinitialize = mmap_deinitialize,
    .allocate = mmap_allocate,
    .deallocate = mmap_deallocate,
    .getaddr = mmap_getaddr,
    .add_mapping_spec = mmap_add_mapping_spec,
#endif
};

struct partaked_shmem_impl *partaked_shmem_mmap_impl(void) {
    return &mmap_impl;
}
