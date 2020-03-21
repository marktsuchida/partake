/*
 * Shared memory allocation for partaked
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

#pragma once

#include "partake_daemon.h"
#include "partake_protocol_builder.h"
#include "partake_tchar.h"


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

TCHAR *partake_alloc_random_name(TCHAR *prefix, size_t random_len);
