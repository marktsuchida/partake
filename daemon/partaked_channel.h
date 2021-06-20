/*
 * Connection access to objects
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_token.h"

#include <stdbool.h>
#include <stddef.h>

struct partaked_handle;
struct partaked_object;
struct partaked_pool;
struct partaked_segment;


struct partaked_channel *partaked_channel_create(struct partaked_pool *pool);

void partaked_channel_destroy(struct partaked_channel *chan);


int partaked_channel_get_segment(struct partaked_channel *chan, uint32_t segno,
        struct partaked_segment **segment);


struct partaked_pool *partaked_channel_get_pool(struct partaked_channel *chan);


// If successful, *handle has refcount 1, open_count 1.
int partaked_channel_alloc_object(struct partaked_channel *chan,
        size_t size, bool clear, uint8_t policy,
        struct partaked_handle **handle);


// Refcount of *handle is unchanged.
int partaked_channel_realloc_object(struct partaked_channel *chan,
        partaked_token token, size_t size, struct partaked_handle **handle);


// If successful, handle open_count is incremented.
int partaked_channel_resume_open_object(struct partaked_channel *chan,
        struct partaked_handle *handle, struct partaked_object *voucher);

// If successful, *handle refcount and open_count are incremented; if busy,
// only refcount is incremented.
int partaked_channel_open_object(struct partaked_channel *chan,
        partaked_token token, uint8_t policy,
        struct partaked_handle **handle, struct partaked_object **voucher);


// If successful, handle for token has refcount and open_count decremented.
int partaked_channel_close_object(struct partaked_channel *chan,
        partaked_token token);


// Refcount for handle for token is unchanged.
int partaked_channel_publish_object(struct partaked_channel *chan,
        partaked_token token);


// Refcount of handle is unchanged.
int partaked_channel_resume_unpublish_object(struct partaked_channel *chan,
        struct partaked_handle *handle, bool clear);

// Refcount of *handle is unchanged; caller must retain if suspending
int partaked_channel_unpublish_object(struct partaked_channel *chan,
        partaked_token token, bool clear, struct partaked_handle **handle);


int partaked_channel_create_voucher(struct partaked_channel *chan,
        partaked_token target_token, struct partaked_object **voucher);


int partaked_channel_discard_voucher(struct partaked_channel *chan,
        partaked_token token, struct partaked_object **target);


// Decrement refcount of handle.
void partaked_channel_release_handle(struct partaked_channel *chan,
        struct partaked_handle *handle);
