/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <ss8str.h>

#include <stdbool.h>
#include <stddef.h>

enum partaked_shmem_type {
    PARTAKED_SHMEM_UNKNOWN,
    PARTAKED_SHMEM_MMAP,
    PARTAKED_SHMEM_SHMGET,
    PARTAKED_SHMEM_WIN32,
};

struct partaked_config {
    ss8str socket;

    size_t size;
    bool force;

    struct {
        enum partaked_shmem_type type;
        struct {
            bool shm_open;
            ss8str shmname;  // if shm_open
            ss8str filename; // if !shm_open
        } mmap;
        struct {
            int key;
            bool huge_pages;
        } shmget;
        struct {
            ss8str filename; // system paging file if empty
            ss8str mappingname;
            bool large_pages;
        } win32;
    } shmem;
};

struct partaked_config *partaked_config_create(void);
void partaked_config_destroy(struct partaked_config *config);
