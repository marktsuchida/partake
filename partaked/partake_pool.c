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

#include "prefix.h"

#include "partake_allocator.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_pool.h"
#include "partake_segment.h"

#include <uthash.h>

#include <assert.h>
#include <string.h>


struct partake_pool {
    // For now, the pool consists of a single segment.
    struct partake_segment *segment;
    void *addr;
    partake_allocator allocator;
    struct partake_object *objects; // Hash table
};


struct partake_pool *partake_pool_create(struct partake_segment *segment) {
    struct partake_pool *ret = partake_malloc(sizeof(*ret));

    ret->segment = segment;
    ret->addr = partake_segment_addr(segment);

    ret->allocator = partake_create_allocator(ret->addr,
            partake_segment_size(segment));
    // We allow allocator to be null (the result of size being too small); it
    // just means we are always out of memory.

    ret->objects = NULL;

    return ret;
}


void partake_pool_destroy(struct partake_pool *pool) {
    if (pool == NULL)
        return;

    assert (pool->objects == NULL);
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
    object->offset = block - (char *)pool->addr;
    object->size = size;
    object->flags = 0;
    object->refcount = 1;
    object->open_count = 0;
    object->exclusive_writer = NULL;

    HASH_ADD(hh, pool->objects, token, sizeof(partake_token), object);

    return object;
}


void partake_pool_destroy_object(struct partake_pool *pool,
        struct partake_object *object) {
    assert (object->refcount == 0);

    HASH_DELETE(hh, pool->objects, object);
    partake_deallocate(pool->allocator, (char *)pool->addr + object->offset);
    partake_free(object);
}


int partake_pool_resize_object(struct partake_pool *pool,
        struct partake_object *object, size_t size) {
    char *block = partake_reallocate(pool->allocator,
            (char *)pool->addr + object->offset, size);
    if (block == NULL)
        return -1;

    object->offset = block - (char *)pool->addr;
    return 0;
}


void partake_pool_rekey_object(struct partake_pool *pool,
        struct partake_object *object, partake_token token) {
    HASH_DELETE(hh, pool->objects, object);
    object->token = token;
    HASH_ADD(hh, pool->objects, token, sizeof(partake_token), object);
}


void partake_pool_clear_object(struct partake_pool *pool,
        struct partake_object *object) {
    memset((char *)pool->addr + object->offset, 0, object->size);
}
