/*
 * Partake shared memory pool
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "prefix.h"

#include "partake_allocator.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_pool.h"
#include "partake_segment.h"
#include "partake_voucherqueue.h"

#include <uthash.h>

#include <assert.h>
#include <string.h>


struct partake_pool {
    // For now, the pool consists of a single segment.
    struct partake_segment *segment;
    void *addr;
    partake_allocator allocator;
    struct partake_object *objects; // Hash table
    struct partake_voucherqueue *voucher_expiration_queue;
};


struct partake_pool *partake_pool_create(uv_loop_t *event_loop,
        struct partake_segment *segment) {
    struct partake_pool *ret = partake_malloc(sizeof(*ret));

    ret->segment = segment;
    ret->addr = partake_segment_addr(segment);

    ret->allocator = partake_create_allocator(ret->addr,
            partake_segment_size(segment));
    // We allow allocator to be null (the result of size being too small); it
    // just means we are always out of memory.

    ret->objects = NULL;

    ret->voucher_expiration_queue =
            partake_voucherqueue_create(event_loop, ret);
    if (!ret->voucher_expiration_queue) {
        partake_free(ret);
        return NULL;
    }

    return ret;
}


void partake_pool_destroy(struct partake_pool *pool) {
    if (pool == NULL)
        return;

    assert (pool->objects == NULL);

    partake_voucherqueue_destroy(pool->voucher_expiration_queue);
    partake_free(pool);
}


struct partake_segment *partake_pool_segment(struct partake_pool *pool) {
    return pool->segment;
}


struct partake_object *partake_pool_find_object(
        struct partake_pool *pool, partake_token token) {
    struct partake_object *ret;
    HASH_FIND(hh, pool->objects, &token, sizeof(partake_token), ret);
    return ret;
}


struct partake_object *partake_pool_create_object(struct partake_pool *pool,
        size_t size, bool clear, partake_token token) {
    struct partake_object *object = partake_malloc(sizeof(*object));

    char *block = partake_allocate(pool->allocator, size, clear);
    if (block == NULL) {
        partake_free(object);
        return NULL;
    }

    object->token = token;
    object->flags = 0;
    object->offset = block - (char *)pool->addr;
    object->size = size;
    object->refcount = 1;
    object->open_count = 0;
    object->exclusive_writer = NULL;
    object->handles_waiting_for_publish = NULL;
    object->handle_waiting_for_sole_ownership = NULL;

    HASH_ADD(hh, pool->objects, token, sizeof(partake_token), object);

    return object;
}


struct partake_object *partake_pool_create_voucher(struct partake_pool *pool,
        partake_token voucher_token, struct partake_object *target) {
    struct partake_object *voucher = partake_malloc(sizeof(*voucher));

    voucher->token = voucher_token;
    voucher->flags = PARTAKE_OBJECT_IS_VOUCHER;
    voucher->target = target;
    voucher->expiration = 0;
    voucher->next = voucher->prev = NULL;

    HASH_ADD(hh, pool->objects, token, sizeof(partake_token), voucher);

    return voucher;
}


void partake_pool_destroy_object(struct partake_pool *pool,
        struct partake_object *object) {
    assert (object->refcount == 0 ||
            (object->flags & PARTAKE_OBJECT_IS_VOUCHER));

    HASH_DELETE(hh, pool->objects, object);

    if (!(object->flags & PARTAKE_OBJECT_IS_VOUCHER)) {
        partake_deallocate(pool->allocator,
                (char *)pool->addr + object->offset);
    }

    partake_free(object);
}


int partake_pool_resize_object(struct partake_pool *pool,
        struct partake_object *object, size_t size) {
    assert (!(object->flags & PARTAKE_OBJECT_IS_VOUCHER));

    char *block = partake_reallocate(pool->allocator,
            (char *)pool->addr + object->offset, size);
    if (block == NULL)
        return -1;

    object->offset = block - (char *)pool->addr;
    return 0;
}


void partake_pool_rekey_object(struct partake_pool *pool,
        struct partake_object *object, partake_token token) {
    assert (!(object->flags & PARTAKE_OBJECT_IS_VOUCHER));

    HASH_DELETE(hh, pool->objects, object);
    object->token = token;
    HASH_ADD(hh, pool->objects, token, sizeof(partake_token), object);
}


void partake_pool_clear_object(struct partake_pool *pool,
        struct partake_object *object) {
    assert (!(object->flags & PARTAKE_OBJECT_IS_VOUCHER));

    memset((char *)pool->addr + object->offset, 0, object->size);
}


struct partake_voucherqueue *partake_pool_get_voucherqueue(
        struct partake_pool *pool) {
    return pool->voucher_expiration_queue;
}
