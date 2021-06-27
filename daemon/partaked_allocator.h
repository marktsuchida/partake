/*
 * Wrapper for dlmalloc to allocate space within our shared memory segment.
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef void *partaked_allocator;

partaked_allocator partaked_create_allocator(void *base, size_t size);

// There is no 'destroy' because all dlmalloc data is stored inside the segment
// it manages.

void *partaked_allocate(partaked_allocator allocator, size_t size, bool clear);

void partaked_deallocate(partaked_allocator allocator, void *addr);

void *partaked_reallocate(partaked_allocator allocator, void *addr,
                          size_t newsize);

void **partaked_allocate_many(partaked_allocator allocator, size_t n,
                              size_t elem_size, void **addrs, bool clear);

void **partaked_allocate_many_sizes(partaked_allocator allocator, size_t n,
                                    size_t *sizes, void **addrs, bool clear);
