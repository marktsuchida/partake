/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

struct partaked_config;

#include "partake_protocol_builder.h"

struct partaked_shmem_impl {
    char *name; // Non-null even if not supported

    // int return values are platform error codes (success = 0)
    int (*initialize)(void **data);
    void (*deinitialize)(void *data);
    int (*allocate)(const struct partaked_config *config, void *data);
    void (*deallocate)(const struct partaked_config *config, void *data);
    void *(*getaddr)(void *data);
    void (*add_mapping_spec)(flatcc_builder_t *b, void *data);
};

// partaked_shmem_*_impl() return pointer to static impl struct (even if only
// 'name' field is non-null).

struct partaked_shmem_impl *partaked_shmem_mmap_impl(void);

struct partaked_shmem_impl *partaked_shmem_shmget_impl(void);

struct partaked_shmem_impl *partaked_shmem_win32_impl(void);
