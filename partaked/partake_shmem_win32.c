/*
 * Win32 shared memory for partaked
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

#include "partake_logging.h"
#include "partake_malloc.h"
#include "partake_shmem.h"
#include "partake_tchar.h"

#ifdef _WIN32

#include <zf_log.h>


struct win32_private_data {
    HANDLE h_file;
    TCHAR *mapping_name;
    bool must_free_mapping_name;
    HANDLE h_mapping;
    void *addr;
};


static int win32_initialize(void **data) {
    *data = partake_malloc(sizeof(struct win32_private_data));
    memset(*data, 0, sizeof(struct win32_private_data));

    struct win32_private_data *d = *data;
    d->h_file = INVALID_HANDLE_VALUE;

    return 0;
}


static void win32_deinitialize(void *data) {
    struct win32_private_data *d = data;
    if (d == NULL) {
        return;
    }

    if (d->h_file != INVALID_HANDLE_VALUE) {
        ZF_LOGF("Deinitializing Win32 segment whose file is still open!");
    }
    if (d->h_mapping != INVALID_HANDLE_VALUE) {
        ZF_LOGF("Deinitializing Win32 segment whose mapping is still open!");
    }
    if (d->addr != NULL) {
        ZF_LOGF("Deinitializing Win32 segment that is still mapped!");
    }

    if (d->must_free_mapping_name) {
        partake_free(d->mapping_name);
    }

    partake_free(data);
}


static int create_file(const struct partake_daemon_config *config,
        struct win32_private_data *d) {
    // We do not need to canonicalize the filename, since we never send it
    // to clients.
    d->h_file = CreateFile(config->shmem.win32.filename,
            GENERIC_READ | GENERIC_WRITE,
            0, NULL,
            config->force ? CREATE_ALWAYS : CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
            NULL);
    if (d->h_file == INVALID_HANDLE_VALUE) {
        DWORD ret = GetLastError();
        char fname[1024], emsg[1024];
        ZF_LOGE("CreateFile: %s: %s",
                partake_strtolog(config->shmem.win32.filename,
                    fname, sizeof(fname)),
                partake_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    char fname[1024];
    ZF_LOGI("CreateFile: %s: HANDLE %p",
            partake_strtolog(config->shmem.win32.filename,
                fname, sizeof(fname)),
            d->h_file);
    return 0;
}


static int create_file_mapping(const struct partake_daemon_config *config,
        struct win32_private_data *d) {
    bool generate_name = config->shmem.win32.name == NULL;
    bool force = config->force && !generate_name;

    char *generated_name = NULL;
    char *name;

    if (!generate_name) {
        name = config->shmem.win32.name;
    }

    int NUM_RETRIES = 100;
    for (int i = 0; i < NUM_RETRIES; ++i) {
        if (generate_name) {
            name = generated_name = partake_alloc_random_name(
                    PARTAKE_TEXT("Local\\"), 32);
        }

        d->h_mapping = CreateFileMapping(d->h_file,
                NULL,
                PAGE_READWRITE | SEC_COMMIT |
                (config->shmem.win32.large_pages ? SEC_LARGE_PAGES : 0),
                sizeof(size_t) > 4 ? config->size >> 32 : 0,
                config->size & UINT_MAX,
                name);
        DWORD ret = GetLastError();
        if (ret != 0) {
            char namebuf[1024], emsg[1024];
            ZF_LOGE("CreateFileMapping: %s: %s",
                    partake_strtolog(name, namebuf, sizeof(namebuf)),
                    partake_strerror(ret, emsg, sizeof(emsg)));
            partake_free(generated_name);
            d->h_mapping = INVALID_HANDLE_VALUE;

            if (generate_name && ret == ERROR_ALREADY_EXISTS) {
                CloseHandle(d->h_mapping);
                continue;
            }
            return ret;
        }

        char namebuf[1024];
        ZF_LOGI("CreateFileMapping: %s: HANDLE %p",
                partake_strtolog(name, namebuf, sizeof(namebuf)),
                d->h_mapping);

        d->mapping_name = name;
        d->must_free_mapping_name = generate_name;
        return 0;
    }

    ZF_LOGE("Giving up after trying %d names", NUM_RETRIES);
    return ERROR_ALREADY_EXISTS;
}


static void close_handle(HANDLE *h) {
    if (*h == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!CloseHandle(*h)) {
        DWORD ret = GetLastError();
        char emsg[1024];
        ZF_LOGE("CloseHandle: HANDLE %p: %s", *h,
                partake_strerror(ret, emsg, sizeof(emsg)));
    }
    else {
        ZF_LOGI("CloseHandle: HANDLE %p", *h);
    }

    // Mark closed regardless of CloseHandle() success, because there is no
    // point in trying again.
    *h = INVALID_HANDLE_VALUE;
}


static int map_memory(const struct partake_daemon_config *config,
        struct win32_private_data *d) {
    d->addr = MapViewOfFile(d->h_mapping,
            FILE_MAP_READ | FILE_MAP_WRITE |
            (config->shmem.win32.large_pages ? FILE_MAP_LARGE_PAGES : 0),
            0, 0, config->size);
    if (d->addr == NULL) {
        DWORD ret = GetLastError();
        char emsg[1024];
        ZF_LOGE("MapViewOfFile: HANDLE %p, %zu bytes: %s",
                d->h_mapping, config->size,
                partake_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    ZF_LOGI("MapViewOfFile: HANDLE %p: %zu bytes at %p",
            d->h_mapping, config->size, d->addr);
    return 0;
}


static int unmap_memory(struct win32_private_data *d) {
    if (d->addr == NULL) {
        return 0;
    }

    DWORD ret = 0;
    if (!UnmapViewOfFile(d->addr)) {
        ret = GetLastError();
        char emsg[1024];
        ZF_LOGE("UnmapViewOfFile: %p: %s", d->addr,
                partake_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }
    else {
        ZF_LOGI("UnmapViewOfFile: %p", d->addr);
    }

    d->addr = NULL;

    return ret;
}


static int win32_allocate(const struct partake_daemon_config *config,
        void *data) {
    struct win32_private_data *d = data;

    if (config->shmem.win32.filename != NULL) {
        int ret = create_file(config, d);
        if (ret != 0)
            return ret;

        // CreateFileMapping() will increase the file size to match the
        // mapping.
        // TODO We should probably truncate the file if its size is greater
        // than the requested size.
    }

    int ret = create_file_mapping(config, d);
    if (ret != 0) {
        close_handle(&d->h_file);
        return ret;
    }

    ret = map_memory(config, d);
    if (ret != 0) {
        close_handle(&d->h_mapping);
        close_handle(&d->h_file);
        return ret;
    }

    return 0;
}


static void win32_deallocate(const struct partake_daemon_config *config,
        void *data) {
    struct win32_private_data *d = data;

    unmap_memory(d);
    close_handle(&d->h_mapping);
    close_handle(&d->h_file);

    // We opened the file with FILE_FLAG_DELETE_ON_CLOSE, so no need to delete.
}


static void *win32_getaddr(void *data) {
    struct win32_private_data *d = data;
    return d->addr;
}

#endif // _WIN32


static struct partake_shmem_impl win32_impl = {
    .name = "Win32 shared memory",
#ifdef _WIN32
    .initialize = win32_initialize,
    .deinitialize = win32_deinitialize,
    .allocate = win32_allocate,
    .deallocate = win32_deallocate,
    .getaddr = win32_getaddr,
#endif
};


struct partake_shmem_impl *partake_shmem_win32_impl(void) {
    return &win32_impl;
}
