/*
 * Connection access to objects
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partake_token.h"

#include <stdbool.h>
#include <stddef.h>

struct partake_handle;
struct partake_object;
struct partake_pool;
struct partake_segment;


struct partake_channel *partake_channel_create(struct partake_pool *pool);

void partake_channel_destroy(struct partake_channel *chan);


int partake_channel_get_segment(struct partake_channel *chan, uint32_t segno,
        struct partake_segment **segment);


struct partake_pool *partake_channel_get_pool(struct partake_channel *chan);


// If successful, *handle has refcount 1, open_count 1.
int partake_channel_alloc_object(struct partake_channel *chan,
        size_t size, bool clear, uint8_t policy,
        struct partake_handle **handle);


// Refcount of *handle is unchanged.
int partake_channel_realloc_object(struct partake_channel *chan,
        partake_token token, size_t size, struct partake_handle **handle);


// If successful, handle open_count is incremented.
int partake_channel_resume_open_object(struct partake_channel *chan,
        struct partake_handle *handle, struct partake_object *voucher);

// If successful, *handle refcount and open_count are incremented; if busy,
// only refcount is incremented.
int partake_channel_open_object(struct partake_channel *chan,
        partake_token token, uint8_t policy,
        struct partake_handle **handle, struct partake_object **voucher);


// If successful, handle for token has refcount and open_count decremented.
int partake_channel_close_object(struct partake_channel *chan,
        partake_token token);


// Refcount for handle for token is unchanged.
int partake_channel_publish_object(struct partake_channel *chan,
        partake_token token);


// Refcount of handle is unchanged.
int partake_channel_resume_unpublish_object(struct partake_channel *chan,
        struct partake_handle *handle, bool clear);

// Refcount of *handle is unchanged; caller must retain if suspending
int partake_channel_unpublish_object(struct partake_channel *chan,
        partake_token token, bool clear, struct partake_handle **handle);


int partake_channel_create_voucher(struct partake_channel *chan,
        partake_token target_token, struct partake_object **voucher);


int partake_channel_discard_voucher(struct partake_channel *chan,
        partake_token token, struct partake_object **target);


// Decrement refcount of handle.
void partake_channel_release_handle(struct partake_channel *chan,
        struct partake_handle *handle);
