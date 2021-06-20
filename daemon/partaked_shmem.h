/*
 * Shared memory allocation for partaked
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_daemon.h"
#include "partaked_tchar.h"

#include "partake_protocol_builder.h"


struct partake_shmem_impl {
    TCHAR *name; // Non-null even if not supported

    // int return values are platform error codes (success = 0)
    int (*initialize)(void **data);
    void (*deinitialize)(void *data);
    int (*allocate)(const struct partake_daemon_config *config, void *data);
    void (*deallocate)(const struct partake_daemon_config *config, void *data);
    void *(*getaddr)(void *data);
    void (*add_mapping_spec)(flatcc_builder_t *b, void *data);
};


// partake_shmem_*_impl() return pointer to static impl struct (even if only
// 'name' field is non-null).

struct partake_shmem_impl *partake_shmem_mmap_impl(void);

struct partake_shmem_impl *partake_shmem_shmget_impl(void);

struct partake_shmem_impl *partake_shmem_win32_impl(void);


int partake_generate_random_int(void);

TCHAR *partake_alloc_random_name(TCHAR *prefix, size_t random_len,
        size_t max_total_len);
