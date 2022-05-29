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

struct partaked_handle;
struct partaked_segment;
struct partaked_voucherqueue;

struct partaked_pool *partaked_pool_create(uv_loop_t *event_loop,
                                           struct partaked_segment *segment);

void partaked_pool_destroy(struct partaked_pool *pool);

struct partaked_segment *partaked_pool_segment(struct partaked_pool *pool);

/*
 * These are low-level functions that do not take into account the state
 * (refcounts, flags) of objects (those need to be managed per-connection).
 */

struct partaked_object *partaked_pool_find_object(struct partaked_pool *pool,
                                                  partaked_token token);

struct partaked_object *partaked_pool_create_object(struct partaked_pool *pool,
                                                    size_t size, bool clear,
                                                    partaked_token token);

struct partaked_object *
partaked_pool_create_voucher(struct partaked_pool *pool,
                             partaked_token voucher_token,
                             struct partaked_object *target);

void partaked_pool_destroy_object(struct partaked_pool *pool,
                                  struct partaked_object *object);

void partaked_pool_rekey_object(struct partaked_pool *pool,
                                struct partaked_object *object,
                                partaked_token token);

void partaked_pool_clear_object(struct partaked_pool *pool,
                                struct partaked_object *object);

struct partaked_voucherqueue *
partaked_pool_get_voucherqueue(struct partaked_pool *pool);
