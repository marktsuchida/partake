/*
 * Shared memory segments for partaked
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

#include "partake_malloc.h"
#include "partake_segment.h"
#include "partake_shmem.h"

#include <zf_log.h>


struct partake_segment {
    // TODO We should have a 'shmem_config' object separate from daemon config
    const struct partake_daemon_config *config; // Non-owning
    struct partake_shmem_impl *shmem_impl;
    void *shmem_data;
};


struct partake_segment *partake_segment_create(
        const struct partake_daemon_config *config) {
    struct partake_shmem_impl *impl;
    switch (config->type) {
        case PARTAKE_SHMEM_MMAP:
            impl = partake_shmem_mmap_impl();
            break;
        case PARTAKE_SHMEM_SHMGET:
            impl = partake_shmem_shmget_impl();
            break;
        case PARTAKE_SHMEM_WIN32:
            impl = partake_shmem_win32_impl();
            break;
        default:
            impl = NULL;
            break;
    }
    if (impl == NULL) {
        ZF_LOGE("Unknown shared memory type");
        return NULL;
    }
    if (impl->allocate == NULL) {
        ZF_LOGE("This platform (or partaked build) does not support %s",
                impl->name);
        return NULL;
    }

    struct partake_segment *segment = partake_malloc(sizeof(*segment));
    segment->config = config;
    segment->shmem_impl = impl;
    segment->shmem_data = NULL;

    int ret = impl->initialize(&segment->shmem_data);
    if (ret != 0) {
        partake_free(segment);
        return NULL;
    }

    ret = impl->allocate(config, segment->shmem_data);
    if (ret != 0) {
        impl->deinitialize(segment->shmem_data);
        partake_free(segment);
        return NULL;
    }

    return segment;
}


void partake_segment_destroy(struct partake_segment *segment) {
    if (segment == NULL)
        return;

    segment->shmem_impl->deallocate(segment->config, segment->shmem_data);
    segment->shmem_impl->deinitialize(segment->shmem_data);
}


void *partake_segment_addr(struct partake_segment *segment) {
    return segment->shmem_impl->getaddr(segment->shmem_data);
}


size_t partake_segment_size(struct partake_segment *segment) {
    return segment->config->size;
}


void partake_segment_add_mapping_spec(struct partake_segment *segment,
        flatcc_builder_t *b) {
    segment->shmem_impl->add_mapping_spec(b, segment->shmem_data);
}
