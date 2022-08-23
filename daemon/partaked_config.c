/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_config.h"

#include "partaked_malloc.h"

#include <ss8str.h>

struct partaked_config *partaked_config_create(void) {
    struct partaked_config *ret =
        partaked_calloc(1, sizeof(struct partaked_config));
    ss8_init(&ret->socket);
    ss8_init(&ret->shmem.mmap.shmname);
    ss8_init(&ret->shmem.mmap.filename);
    ss8_init(&ret->shmem.win32.filename);
    ss8_init(&ret->shmem.win32.mappingname);
    return ret;
}

void partaked_config_destroy(struct partaked_config *config) {
    if (!config)
        return;
    ss8_destroy(&config->socket);
    ss8_destroy(&config->shmem.mmap.shmname);
    ss8_destroy(&config->shmem.mmap.filename);
    ss8_destroy(&config->shmem.win32.filename);
    ss8_destroy(&config->shmem.win32.mappingname);
    partaked_free(config);
}
