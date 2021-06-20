/*
 * Partake voucher expiration queue
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <uv.h>

struct partake_object;
struct partake_pool;
struct partake_voucherqueue;


struct partake_voucherqueue *partake_voucherqueue_create(uv_loop_t *event_loop,
        struct partake_pool *pool);

void partake_voucherqueue_destroy(struct partake_voucherqueue *queue);


void partake_voucherqueue_enqueue(struct partake_voucherqueue *queue,
        struct partake_object *voucher);

void partake_voucherqueue_remove(struct partake_voucherqueue *queue,
        struct partake_object *voucher);
