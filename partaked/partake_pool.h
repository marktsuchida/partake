/*
 * Partake shared memory pool
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

#include "partake_token.h"

#include <stddef.h>

struct partake_object;
struct partake_pool;


struct partake_pool *partake_pool_create(void *addr, size_t size);

void partake_pool_destroy(struct partake_pool *pool);


/*
 * These are low-level functions that do not take into account the state
 * (refcounts, flags) of objects (those need to be managed per-connection).
 */

struct partake_object *partake_pool_find_object(
        struct partake_pool *pool, partake_token token);

struct partake_object *partake_pool_create_object(
        struct partake_pool *pool, size_t size, partake_token token);

void partake_pool_destroy_object(struct partake_pool *pool,
        struct partake_object *object);

int partake_pool_resize_object(struct partake_pool *pool,
        struct partake_object *object, size_t size);

void partake_pool_rekey_object(struct partake_pool *pool,
        struct partake_object *object, partake_token token);
