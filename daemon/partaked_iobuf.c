/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_iobuf.h"
#include "partaked_malloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_ALIGNMENT 8 // 8 is sufficient for our flatbuffers

static struct partaked_iobuf *freelist;

struct partaked_iobuf *partaked_iobuf_create(size_t size) {
    struct partaked_iobuf *iobuf;
    if (size == PARTAKED_IOBUF_STD_SIZE && freelist != NULL) {
        iobuf = freelist;
        freelist = iobuf->management.next_free;
        iobuf->management.next_free = NULL;
    } else {
        size_t header_size = sizeof(struct partaked_iobuf);
        header_size =
            (header_size + BUFFER_ALIGNMENT - 1) & ~(BUFFER_ALIGNMENT - 1);

        iobuf = partaked_malloc(header_size + size);
        iobuf->addr_to_free = iobuf;
        iobuf->buffer = (char *)iobuf + header_size;
        iobuf->capacity = size;
    }

    iobuf->management.refcount = 1;

    return iobuf;
}

void partaked_iobuf_destroy(struct partaked_iobuf *iobuf) {
    assert(iobuf->management.refcount == 0);
    assert(iobuf->management.next_free == 0);

    if (iobuf->capacity == PARTAKED_IOBUF_STD_SIZE) {
        iobuf->management.next_free = freelist;
        freelist = iobuf;
    } else {
        partaked_free(iobuf->addr_to_free);
    }
}

void partaked_iobuf_release_freelist(void) {
    while (freelist != NULL) {
        struct partaked_iobuf *iobuf = freelist;
        freelist = iobuf->management.next_free;
        partaked_free(iobuf->addr_to_free);
    }
}
