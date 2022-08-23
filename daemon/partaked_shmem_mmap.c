/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_shmem.h"

#include "partaked_config.h"
#include "partaked_logging.h"
#include "partaked_malloc.h"
#include "partaked_random.h"

#ifndef _WIN32

#include <ss8str.h>
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
    ss8str shmname; // non-empty indicates shm/file created
    void *addr;     // non-null indicates mapped
};

static int mmap_initialize(void **data) {
    *data = partaked_calloc(1, sizeof(struct mmap_private_data));
    struct mmap_private_data *d = *data;
    ss8_init(&d->shmname);
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
    if (!ss8_is_empty(&d->shmname)) {
        ZF_LOGF("Deinitializing mmap segment whose shm/file still exists!");
    }

    ss8_destroy(&d->shmname);
    partaked_free(data);
}

static int create_posix_shm(const struct partaked_config *config,
                            struct mmap_private_data *d) {
    bool generate_name = ss8_is_empty(&config->shmem.mmap.shmname);
    bool force = config->force && !generate_name;

    const char *generated_name = NULL;
    ss8str name;
    ss8_init(&name);

    int ret = 0;

    if (!generate_name)
        ss8_copy(&name, &config->shmem.mmap.shmname);

    int NUM_RETRIES = 100;
    for (int i = 0; i < NUM_RETRIES; ++i) {
        if (generate_name) {
            generated_name =
                partaked_alloc_random_name("/", 32, MAX_SHM_OPEN_NAME_LEN);
            ss8_copy_cstr(&name, generated_name);
        }

        errno = 0;
        d->fd = shm_open(ss8_const_cstr(&name),
                         O_RDWR | O_CREAT | (force ? 0 : O_EXCL), 0666);
        ret = errno;
        if (d->fd < 0) {
            char emsg[1024];
            ZF_LOGE("shm_open: %s: %s", ss8_const_cstr(&name),
                    partaked_strerror(ret, emsg, sizeof(emsg)));
            partaked_free(generated_name);

            if (generate_name && ret == EEXIST) {
                continue;
            }
            goto exit;
        }
        ZF_LOGI("shm_open: %s: fd = %d", ss8_const_cstr(&name), d->fd);

        ss8_copy(&d->shmname, &name);
        d->fd_open = true;
        goto exit;
    }

    ZF_LOGE("Giving up after trying %d names", NUM_RETRIES);
    ret = EEXIST;

exit:
    ss8_destroy(&name);
    return ret;
}

static int unlink_posix_shm(struct mmap_private_data *d) {
    if (ss8_is_empty(&d->shmname)) {
        return 0;
    }

    int ret = 0;
    errno = 0;
    if (shm_unlink(ss8_const_cstr(&d->shmname)) != 0) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("shm_unlink: %s: %s", ss8_const_cstr(&d->shmname),
                partaked_strerror(ret, emsg, sizeof(emsg)));
    }
    ZF_LOGI("shm_unlink: %s", ss8_const_cstr(&d->shmname));

    // Marked unlinked even on error; there is nothing further we can do.
    ss8_clear(&d->shmname);

    return ret;
}

static int canonicalize_filename(const ss8str *name, ss8str *canonical) {
    if (ss8_equals_ch(name, '/')) { // Get edge case out of the way
        ss8_copy(canonical, name);
        return 0;
    }

    int ret = 0;

    // We need to run realpath() on the directory, since the file may not
    // exist yet. Some implementations of dirname() and basename() can modify
    // the given path, so we need to make copies.

    ss8str wk;
    ss8_init(&wk);

    ss8_copy(&wk, name);
    errno = 0;
    char *dirnm = dirname(ss8_cstr(&wk));
    if (dirnm == NULL) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("dirname: %s: %s", ss8_const_cstr(name),
                partaked_strerror(ret, emsg, sizeof(emsg)));
        goto exit;
    }

    ss8_copy(&wk, name);
    errno = 0;
    char *basnm = basename(ss8_cstr(&wk));
    if (basnm == NULL) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("basename: %s: %s", ss8_const_cstr(name),
                partaked_strerror(ret, emsg, sizeof(emsg)));
        goto exit;
    }

    ss8str rdname;
    ss8_init(&rdname);
    ss8_set_len(&rdname, PATH_MAX);
    errno = 0;
    char *realdirnm = realpath(dirnm, ss8_cstr(&rdname));
    if (realdirnm == NULL) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("realpath: %s: %s", dirnm,
                partaked_strerror(ret, emsg, sizeof(emsg)));
        goto exit;
    }

    ss8_copy_cstr(canonical, realdirnm);
    ss8_cat_ch(canonical, '/');
    ss8_cat_cstr(canonical, basnm);

exit:
    ss8_destroy(&rdname);
    ss8_destroy(&wk);
    return ret;
}

static int create_file_shm(const struct partaked_config *config,
                           struct mmap_private_data *d) {
    // We need to use a canonicalized path, because it will be passed to
    // clients for them to also open.
    int ret = canonicalize_filename(&config->shmem.mmap.filename, &d->shmname);
    if (ret != 0)
        return ret;

    errno = 0;
    d->fd = open(ss8_const_cstr(&d->shmname),
                 O_RDWR | O_CREAT | (config->force ? 0 : O_EXCL) | O_CLOEXEC,
                 0666);
    ret = errno;
    if (d->fd < 0) {
        char emsg[1024];
        ZF_LOGE("open: %s: %s", ss8_const_cstr(&d->shmname),
                partaked_strerror(ret, emsg, sizeof(emsg)));
        ss8_clear(&d->shmname);
        return ret;
    }
    ZF_LOGI("open: %s: fd = %d", ss8_const_cstr(&d->shmname), d->fd);

    d->fd_open = true;
    return 0;
}

static int unlink_file_shm(struct mmap_private_data *d) {
    if (ss8_is_empty(&d->shmname))
        return 0;

    int ret = 0;
    errno = 0;
    if (unlink(ss8_const_cstr(&d->shmname)) != 0) {
        ret = errno;
        char emsg[1024];
        ZF_LOGE("unlink: %s: %s", ss8_const_cstr(&d->shmname),
                partaked_strerror(ret, emsg, sizeof(emsg)));
    }
    ZF_LOGI("unlink: %s", ss8_const_cstr(&d->shmname));

    // Marked unlinked even on error; there is nothing further we can do.
    ss8_clear(&d->shmname);

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

static int mmap_allocate(const struct partaked_config *config, void *data) {
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

static void mmap_deallocate(const struct partaked_config *config, void *data) {
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
    partake_protocol_PosixMmapSpec_name_create_str(
        b, ss8_const_cstr(&d->shmname));
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
