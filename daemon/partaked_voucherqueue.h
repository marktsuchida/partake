/*
 * Partake voucher expiration queue
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <uv.h>

struct partaked_object;
struct partaked_pool;
struct partaked_voucherqueue;

struct partaked_voucherqueue *
partaked_voucherqueue_create(uv_loop_t *event_loop,
                             struct partaked_pool *pool);

void partaked_voucherqueue_destroy(struct partaked_voucherqueue *queue);

void partaked_voucherqueue_enqueue(struct partaked_voucherqueue *queue,
                                   struct partaked_object *voucher);

void partaked_voucherqueue_remove(struct partaked_voucherqueue *queue,
                                  struct partaked_object *voucher);
