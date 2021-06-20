/*
 * Reference-counted buffers for request/response
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_iobuf.h"
#include "partaked_malloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


#define BUFFER_ALIGNMENT 8 // 8 is sufficient for our flatbuffers


static struct partake_iobuf *freelist;


struct partake_iobuf *partake_iobuf_create(size_t size) {
    struct partake_iobuf *iobuf;
    if (size == PARTAKE_IOBUF_STD_SIZE && freelist != NULL) {
        iobuf = freelist;
        freelist = iobuf->management.next_free;
        iobuf->management.next_free = NULL;
    }
    else {
        size_t header_size = sizeof(struct partake_iobuf);
        header_size = (header_size + BUFFER_ALIGNMENT - 1) &
            ~(BUFFER_ALIGNMENT - 1);

        iobuf = partake_malloc(header_size + size);
        iobuf->addr_to_free = iobuf;
        iobuf->buffer = (char *)iobuf + header_size;
        iobuf->capacity = size;
    }

    iobuf->management.refcount = 1;

    return iobuf;
}


void partake_iobuf_destroy(struct partake_iobuf *iobuf) {
    assert (iobuf->management.refcount == 0);
    assert (iobuf->management.next_free == 0);

    if (iobuf->capacity == PARTAKE_IOBUF_STD_SIZE) {
        iobuf->management.next_free = freelist;
        freelist = iobuf;
    }
    else {
        partake_free(iobuf->addr_to_free);
    }
}


void partake_iobuf_release_freelist(void) {
    while (freelist != NULL) {
        struct partake_iobuf *iobuf = freelist;
        freelist = iobuf->management.next_free;
        partake_free(iobuf->addr_to_free);
    }
}
