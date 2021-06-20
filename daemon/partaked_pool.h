/*
 * Partake shared memory pool
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_token.h"

#include <uv.h>

#include <stdbool.h>
#include <stddef.h>

struct partake_handle;
struct partake_segment;
struct partake_voucherqueue;


struct partake_pool *partake_pool_create(uv_loop_t *event_loop,
        struct partake_segment *segment);

void partake_pool_destroy(struct partake_pool *pool);

struct partake_segment *partake_pool_segment(struct partake_pool *pool);


/*
 * These are low-level functions that do not take into account the state
 * (refcounts, flags) of objects (those need to be managed per-connection).
 */

struct partake_object *partake_pool_find_object(
        struct partake_pool *pool, partake_token token);

struct partake_object *partake_pool_create_object(struct partake_pool *pool,
        size_t size, bool clear, partake_token token);

struct partake_object *partake_pool_create_voucher(struct partake_pool *pool,
        partake_token voucher_token, struct partake_object *target);

void partake_pool_destroy_object(struct partake_pool *pool,
        struct partake_object *object);

int partake_pool_resize_object(struct partake_pool *pool,
        struct partake_object *object, size_t size);

void partake_pool_rekey_object(struct partake_pool *pool,
        struct partake_object *object, partake_token token);

void partake_pool_clear_object(struct partake_pool *pool,
        struct partake_object *object);

struct partake_voucherqueue *partake_pool_get_voucherqueue(
        struct partake_pool *pool);
