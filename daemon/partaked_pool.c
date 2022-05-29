/*
 * Partake shared memory pool
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_allocator.h"
#include "partaked_malloc.h"
#include "partaked_object.h"
#include "partaked_pool.h"
#include "partaked_segment.h"
#include "partaked_voucherqueue.h"

#include <uthash.h>
#include <uv.h>

#include <assert.h>
#include <string.h>

struct partaked_pool {
    // For now, the pool consists of a single segment.
    struct partaked_segment *segment;
    void *addr;
    partaked_allocator allocator;
    struct partaked_object *objects; // Hash table
    uv_loop_t *event_loop;
    struct partaked_voucherqueue *voucher_expiration_queue;
};

struct partaked_pool *partaked_pool_create(uv_loop_t *event_loop,
                                           struct partaked_segment *segment) {
    struct partaked_pool *ret = partaked_malloc(sizeof(*ret));

    ret->segment = segment;
    ret->addr = partaked_segment_addr(segment);

    ret->allocator =
        partaked_create_allocator(ret->addr, partaked_segment_size(segment));
    // We allow allocator to be null (the result of size being too small); it
    // just means we are always out of memory.

    ret->objects = NULL;

    ret->event_loop = event_loop;

    // The voucher queue is created lazily, because it requires the event loop
    // to be running (whereas we allow the pool to be created before starting
    // the event loop).
    ret->voucher_expiration_queue = NULL;

    return ret;
}

void partaked_pool_destroy(struct partaked_pool *pool) {
    if (pool == NULL)
        return;

    assert(pool->objects == NULL);

    partaked_voucherqueue_destroy(pool->voucher_expiration_queue);
    partaked_free(pool);
}

struct partaked_segment *partaked_pool_segment(struct partaked_pool *pool) {
    return pool->segment;
}

struct partaked_object *partaked_pool_find_object(struct partaked_pool *pool,
                                                  partaked_token token) {
    struct partaked_object *ret;
    HASH_FIND(hh, pool->objects, &token, sizeof(partaked_token), ret);
    return ret;
}

struct partaked_object *partaked_pool_create_object(struct partaked_pool *pool,
                                                    size_t size, bool clear,
                                                    partaked_token token) {
    struct partaked_object *object = partaked_malloc(sizeof(*object));

    char *block = partaked_allocate(pool->allocator, size, clear);
    if (block == NULL) {
        partaked_free(object);
        return NULL;
    }

    object->token = token;
    object->flags = 0;
    object->offset = block - (char *)pool->addr;
    object->size = size;
    object->refcount = 1;
    object->open_count = 0;
    object->exclusive_writer = NULL;
    object->handles_waiting_for_share = NULL;
    object->handle_waiting_for_sole_ownership = NULL;

    HASH_ADD(hh, pool->objects, token, sizeof(partaked_token), object);

    return object;
}

struct partaked_object *
partaked_pool_create_voucher(struct partaked_pool *pool,
                             partaked_token voucher_token,
                             struct partaked_object *target) {
    struct partaked_object *voucher = partaked_malloc(sizeof(*voucher));

    voucher->token = voucher_token;
    voucher->flags = PARTAKED_OBJECT_IS_VOUCHER;
    voucher->target = target;
    voucher->expiration = 0;
    voucher->next = voucher->prev = NULL;

    HASH_ADD(hh, pool->objects, token, sizeof(partaked_token), voucher);

    return voucher;
}

void partaked_pool_destroy_object(struct partaked_pool *pool,
                                  struct partaked_object *object) {
    assert(object->refcount == 0 ||
           (object->flags & PARTAKED_OBJECT_IS_VOUCHER));

    HASH_DELETE(hh, pool->objects, object);

    if (!(object->flags & PARTAKED_OBJECT_IS_VOUCHER)) {
        partaked_deallocate(pool->allocator,
                            (char *)pool->addr + object->offset);
    }

    partaked_free(object);
}

void partaked_pool_rekey_object(struct partaked_pool *pool,
                                struct partaked_object *object,
                                partaked_token token) {
    assert(!(object->flags & PARTAKED_OBJECT_IS_VOUCHER));

    HASH_DELETE(hh, pool->objects, object);
    object->token = token;
    HASH_ADD(hh, pool->objects, token, sizeof(partaked_token), object);
}

void partaked_pool_clear_object(struct partaked_pool *pool,
                                struct partaked_object *object) {
    assert(!(object->flags & PARTAKED_OBJECT_IS_VOUCHER));

    memset((char *)pool->addr + object->offset, 0, object->size);
}

struct partaked_voucherqueue *
partaked_pool_get_voucherqueue(struct partaked_pool *pool) {
    if (pool->voucher_expiration_queue == NULL) {
        pool->voucher_expiration_queue =
            partaked_voucherqueue_create(pool->event_loop, pool);
    }
    return pool->voucher_expiration_queue;
}
