/*
 * Win32 shared memory for partaked
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"
#include "partaked_shmem.h"

#include "partaked_logging.h"
#include "partaked_malloc.h"
#include "partaked_random.h"
#include "partaked_tchar.h"

#ifdef _WIN32

#include <zf_log.h>

struct win32_private_data {
    HANDLE h_file;
    TCHAR *mapping_name;
    bool must_free_mapping_name;
    HANDLE h_mapping;
    bool large_pages;
    void *addr;
};

static int win32_initialize(void **data) {
    *data = partaked_malloc(sizeof(struct win32_private_data));
    memset(*data, 0, sizeof(struct win32_private_data));

    struct win32_private_data *d = *data;
    d->h_file = INVALID_HANDLE_VALUE;
    d->h_mapping = INVALID_HANDLE_VALUE;

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
        partaked_free(d->mapping_name);
    }

    partaked_free(data);
}

static int add_lock_memory_privilage(void) {
    HANDLE h_token;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &h_token)) {
        DWORD ret = GetLastError();
        char emsg[1024];
        ZF_LOGE("OpenProcessToken: %s",
                partaked_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    LUID lock_mem_luid;
    if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &lock_mem_luid)) {
        DWORD ret = GetLastError();
        char name[1024], emsg[1024];
        ZF_LOGE("LookupPrivilageValue: %s: %s",
                partaked_strtolog(SE_LOCK_MEMORY_NAME, name, sizeof(name)),
                partaked_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    TOKEN_PRIVILEGES privs;
    privs.PrivilegeCount = 1;
    privs.Privileges[0].Luid = lock_mem_luid;
    privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL ok = AdjustTokenPrivileges(h_token, FALSE, &privs, sizeof(privs), NULL,
                                    NULL);
    DWORD ret = GetLastError();
    if (!ok || ret == ERROR_NOT_ALL_ASSIGNED) {
        char name[1024], emsg[1024];
        ZF_LOGE("AdjustTokenPrivileges: %s: %s",
                partaked_strtolog(SE_LOCK_MEMORY_NAME, name, sizeof(name)),
                partaked_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    CloseHandle(h_token);
    return 0;
}

static int create_file(const struct partaked_daemon_config *config,
                       struct win32_private_data *d) {
    // We do not need to canonicalize the filename, since we never send it
    // to clients.
    d->h_file =
        CreateFile(config->shmem.win32.filename, GENERIC_READ | GENERIC_WRITE,
                   0, NULL, config->force ? CREATE_ALWAYS : CREATE_NEW,
                   FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (d->h_file == INVALID_HANDLE_VALUE) {
        DWORD ret = GetLastError();
        char fname[1024], emsg[1024];
        ZF_LOGE("CreateFile: %s: %s",
                partaked_strtolog(config->shmem.win32.filename, fname,
                                  sizeof(fname)),
                partaked_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    char fname[1024];
    ZF_LOGI(
        "CreateFile: %s: HANDLE %p",
        partaked_strtolog(config->shmem.win32.filename, fname, sizeof(fname)),
        d->h_file);
    return 0;
}

static int create_file_mapping(const struct partaked_daemon_config *config,
                               struct win32_private_data *d) {
    bool generate_name = config->shmem.win32.name == NULL;
    bool force = config->force && !generate_name;

    if (config->size == 0) {
        ZF_LOGI("CreateFileMapping skipped due to zero size");
        return 0;
    }

    d->large_pages = config->shmem.win32.large_pages;

    char *generated_name = NULL;
    char *name;

    if (!generate_name) {
        name = config->shmem.win32.name;
    }

    int NUM_RETRIES = 100;
    for (int i = 0; i < NUM_RETRIES; ++i) {
        if (generate_name) {
            name = generated_name =
                partaked_alloc_random_name(PARTAKED_TEXT("Local\\"), 32, 255);
        }

        d->h_mapping =
            CreateFileMapping(d->h_file, NULL,
                              PAGE_READWRITE | SEC_COMMIT |
                                  (d->large_pages ? SEC_LARGE_PAGES : 0),
                              sizeof(size_t) > 4 ? config->size >> 32 : 0,
                              config->size & UINT_MAX, name);
        DWORD ret = GetLastError();
        if (ret != 0) {
            char namebuf[1024], emsg[1024];
            ZF_LOGE("CreateFileMapping: %s: %s",
                    partaked_strtolog(name, namebuf, sizeof(namebuf)),
                    partaked_strerror(ret, emsg, sizeof(emsg)));
            partaked_free(generated_name);
            d->h_mapping = INVALID_HANDLE_VALUE;

            if (generate_name && ret == ERROR_ALREADY_EXISTS) {
                CloseHandle(d->h_mapping);
                continue;
            }
            return ret;
        }

        char namebuf[1024];
        ZF_LOGI("CreateFileMapping: %s: HANDLE %p",
                partaked_strtolog(name, namebuf, sizeof(namebuf)),
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
                partaked_strerror(ret, emsg, sizeof(emsg)));
    } else {
        ZF_LOGI("CloseHandle: HANDLE %p", *h);
    }

    // Mark closed regardless of CloseHandle() success, because there is no
    // point in trying again.
    *h = INVALID_HANDLE_VALUE;
}

static int map_memory(const struct partaked_daemon_config *config,
                      struct win32_private_data *d) {
    if (config->size == 0) {
        ZF_LOGI("MapViewOfFile skipped due to zero size");
        return 0;
    }

    d->addr = MapViewOfFile(d->h_mapping,
                            FILE_MAP_READ | FILE_MAP_WRITE |
                                (d->large_pages ? FILE_MAP_LARGE_PAGES : 0),
                            0, 0, config->size);
    if (d->addr == NULL) {
        DWORD ret = GetLastError();
        char emsg[1024];
        ZF_LOGE("MapViewOfFile: HANDLE %p, %zu bytes: %s", d->h_mapping,
                config->size, partaked_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    }

    ZF_LOGI("MapViewOfFile: HANDLE %p: %zu bytes at %p", d->h_mapping,
            config->size, d->addr);
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
                partaked_strerror(ret, emsg, sizeof(emsg)));
        return ret;
    } else {
        ZF_LOGI("UnmapViewOfFile: %p", d->addr);
    }

    d->addr = NULL;

    return ret;
}

static int win32_allocate(const struct partaked_daemon_config *config,
                          void *data) {
    struct win32_private_data *d = data;

    if (config->shmem.win32.large_pages) {
        int ret = add_lock_memory_privilage();
        if (ret != 0)
            return ret;
    }

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

static void win32_deallocate(const struct partaked_daemon_config *config,
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

static void win32_add_mapping_spec(flatcc_builder_t *b, void *data) {
    struct win32_private_data *d = data;

    partake_protocol_SegmentSpec_spec_Win32FileMappingSpec_start(b);

    char buf[1024];
    partake_protocol_Win32FileMappingSpec_name_create_str(
        b, partaked_tstrtoutf8(d->mapping_name, buf, sizeof(buf)));

    partake_protocol_Win32FileMappingSpec_use_large_pages_add(b,
                                                              d->large_pages);

    partake_protocol_SegmentSpec_spec_Win32FileMappingSpec_end(b);
}

#endif // _WIN32

static struct partaked_shmem_impl win32_impl = {
    .name = "Win32 shared memory",
#ifdef _WIN32
    .initialize = win32_initialize,
    .deinitialize = win32_deinitialize,
    .allocate = win32_allocate,
    .deallocate = win32_deallocate,
    .getaddr = win32_getaddr,
    .add_mapping_spec = win32_add_mapping_spec,
#endif
};

struct partaked_shmem_impl *partaked_shmem_win32_impl(void) {
    return &win32_impl;
}
