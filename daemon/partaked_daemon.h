/*
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_tchar.h"

#include <stdbool.h>
#include <stddef.h>

struct partaked_connection;
struct partaked_daemon;


enum partaked_shmem_type {
    PARTAKED_SHMEM_UNKNOWN,
    PARTAKED_SHMEM_MMAP,
    PARTAKED_SHMEM_SHMGET,
    PARTAKED_SHMEM_WIN32,
};


struct partaked_daemon_config {
    const TCHAR *socket;
    // 'socket' may point to 'socket_buf' or elsewhere
    TCHAR socket_buf[96];

    size_t size;
    bool force;

    enum partaked_shmem_type type;
    union {
        struct {
            bool shm_open;
            TCHAR *shmname;
            TCHAR *filename;
        } mmap;
        struct {
            int key;
            bool huge_pages;
        } shmget;
        struct {
            TCHAR *filename; // NULL for system paging file
            TCHAR *name;
            bool large_pages;
        } win32;
    } shmem;
};


// Remove connection from the list to be closed upon server shutdown
void partaked_daemon_remove_connection(struct partaked_daemon *daemon,
        struct partaked_connection *conn);


int partaked_daemon_run(const struct partaked_daemon_config *config);
