/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

enum partaked_shmem_type {
    PARTAKED_SHMEM_UNKNOWN,
    PARTAKED_SHMEM_MMAP,
    PARTAKED_SHMEM_SHMGET,
    PARTAKED_SHMEM_WIN32,
};

struct partaked_daemon_config {
    const char *socket;
    // 'socket' may point to 'socket_buf' or elsewhere
    char socket_buf[96];

    size_t size;
    bool force;

    enum partaked_shmem_type type;
    union {
        struct {
            bool shm_open;
            char *shmname;
            char *filename;
        } mmap;
        struct {
            int key;
            bool huge_pages;
        } shmget;
        struct {
            char *filename; // NULL for system paging file
            char *name;
            bool large_pages;
        } win32;
    } shmem;
};
