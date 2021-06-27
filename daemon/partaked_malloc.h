/*
 * Local memory allocation for partaked
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stddef.h>

void partaked_initialize_malloc(void);

/*
 * All allocation of non-shared memory in partaked uses these functions.
 *
 * Allocation failure is handled, so return values need not be checked for
 * NULL.
 */

void *partaked_malloc(size_t size);
void *partaked_realloc(void *ptr, size_t size);
void *partaked_calloc(size_t n, size_t size);
void partaked_free(const void *ptr);
