/*
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_tchar.h"

#include <stdbool.h>
#include <stddef.h>

struct partake_connection;
struct partake_daemon;


enum partake_shmem_type {
    PARTAKE_SHMEM_UNKNOWN,
    PARTAKE_SHMEM_MMAP,
    PARTAKE_SHMEM_SHMGET,
    PARTAKE_SHMEM_WIN32,
};


struct partake_daemon_config {
    const TCHAR *socket;
    // 'socket' may point to 'socket_buf' or elsewhere
    TCHAR socket_buf[96];

    size_t size;
    bool force;

    enum partake_shmem_type type;
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
void partake_daemon_remove_connection(struct partake_daemon *daemon,
        struct partake_connection *conn);


int partake_daemon_run(const struct partake_daemon_config *config);
