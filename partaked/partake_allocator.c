/*
 * Wrapper for dlmalloc to allocate space within our shared memory segment.
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

#include <stdbool.h>

// Note: we #include dlmalloc's malloc.c below.

#define ONLY_MSPACES 1

#ifdef __GNUC__
#   define DLMALLOC_EXPORT static __attribute__((unused))
#else
#   define DLMALLOC_EXPORT static
#endif

// We disable both MOCRECORE and MMAP, instead supplying the whole segment to
// create_mspace_with_base().
#define HAVE_MORECORE 0
#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define DEFAULT_TRIM_THRESHOLD MAX_SIZE_T // Do not trim
#define DEFAULT_MMAP_THRESHOLD MAX_SIZE_T // No individual mmap
#define MAX_RELEASE_CHECK_RATE MAX_SIZE_T // No discontiguous trimming

// Alignment supporting up to AVX512. We could make this user-configurable, but
// it cannot be configured per-mspace and only supports up to 128 bytes.
// Alternatively we could call memalign() instead of malloc() -- presumably we
// will not get any additional fragmentation if our alignment is always the
// same.
#define MALLOC_ALIGNMENT 64

#define USE_BUILTIN_FFS 1

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <Windows.h>
#   undef WIN32
#   define LACKS_STRINGS_H
#   define LACKS_UNISTD_H
#   define LACKS_SYS_PARAM_H
#endif


#undef _WIN32 // Only way to tell dlmalloc not to use Win32 API
#include "dlmalloc/malloc.c"


partake_allocator partake_create_allocator(void *base, size_t size) {
    mspace allocator = create_mspace_with_base(base, size, 0);
    if (allocator != NULL) {
        mspace_track_large_chunks(allocator, 1);
    }
    return allocator;
}


void *partake_allocate(partake_allocator allocator, size_t size, bool clear) {
    if (allocator == NULL) {
        return NULL;
    }
    return clear ?
        mspace_calloc(allocator, 1, size) :
        mspace_malloc(allocator, size);
}


void partake_deallocate(partake_allocator allocator, void *addr) {
    if (allocator == NULL) {
        return;
    }
    mspace_free(allocator, addr);
}


void *partake_reallocate(partake_allocator allocator,
        void *addr, size_t newsize) {
    if (allocator == NULL) {
        return NULL;
    }
    return mspace_realloc(allocator, addr, newsize);
}


void **partake_allocate_many(partake_allocator allocator,
        size_t n, size_t elem_size, void **addrs, bool clear) {
    if (allocator == NULL) {
        if (addrs != NULL) {
            memset(addrs, 0, n * sizeof(void *));
        }
        return NULL;
    }
    (void)clear;
    return mspace_independent_calloc(allocator, n, elem_size, addrs);
}


void **partake_allocate_many_sizes(partake_allocator allocator,
        size_t n, size_t *sizes, void **addrs, bool clear) {
    if (allocator == NULL) {
        if (addrs != NULL) {
            memset(addrs, 0, n * sizeof(void *));
        }
        return NULL;
    }
    (void)clear;
    return mspace_independent_comalloc(allocator, n, sizes, addrs);
}
