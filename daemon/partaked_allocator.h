/*
 * Wrapper for dlmalloc to allocate space within our shared memory segment.
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>


typedef void *partake_allocator;


partake_allocator partake_create_allocator(void *base, size_t size);

// There is no 'destroy' because all dlmalloc data is stored inside the segment
// it manages.

void *partake_allocate(partake_allocator allocator, size_t size, bool clear);

void partake_deallocate(partake_allocator allocator, void *addr);

void *partake_reallocate(partake_allocator allocator,
        void *addr, size_t newsize);

void **partake_allocate_many(partake_allocator allocator,
        size_t n, size_t elem_size, void **addrs, bool clear);

void **partake_allocate_many_sizes(partake_allocator allocator,
        size_t n, size_t *sizes, void **addrs, bool clear);
