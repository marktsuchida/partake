/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_config.h"
#include "partaked_malloc.h"
#include "partaked_segment.h"
#include "partaked_shmem.h"

#include <zf_log.h>

struct partaked_segment {
    // TODO We should have a 'shmem_config' object separate from daemon config
    const struct partaked_config *config; // Non-owning
    struct partaked_shmem_impl *shmem_impl;
    void *shmem_data;
};

struct partaked_segment *
partaked_segment_create(const struct partaked_config *config) {
    struct partaked_shmem_impl *impl;
    switch (config->shmem.type) {
    case PARTAKED_SHMEM_MMAP:
        impl = partaked_shmem_mmap_impl();
        break;
    case PARTAKED_SHMEM_SHMGET:
        impl = partaked_shmem_shmget_impl();
        break;
    case PARTAKED_SHMEM_WIN32:
        impl = partaked_shmem_win32_impl();
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

    struct partaked_segment *segment = partaked_malloc(sizeof(*segment));
    segment->config = config;
    segment->shmem_impl = impl;
    segment->shmem_data = NULL;

    int ret = impl->initialize(&segment->shmem_data);
    if (ret != 0) {
        partaked_free(segment);
        return NULL;
    }

    ret = impl->allocate(config, segment->shmem_data);
    if (ret != 0) {
        impl->deinitialize(segment->shmem_data);
        partaked_free(segment);
        return NULL;
    }

    return segment;
}

void partaked_segment_destroy(struct partaked_segment *segment) {
    if (segment == NULL)
        return;

    segment->shmem_impl->deallocate(segment->config, segment->shmem_data);
    segment->shmem_impl->deinitialize(segment->shmem_data);
    partaked_free(segment);
}

void *partaked_segment_addr(struct partaked_segment *segment) {
    return segment->shmem_impl->getaddr(segment->shmem_data);
}

size_t partaked_segment_size(struct partaked_segment *segment) {
    return segment->config->size;
}

void partaked_segment_add_mapping_spec(struct partaked_segment *segment,
                                       flatcc_builder_t *b) {
    segment->shmem_impl->add_mapping_spec(b, segment->shmem_data);
}
