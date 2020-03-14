#pragma once

#include "partake_tchar.h"

#include <stdbool.h>
#include <stddef.h>


enum partake_shmem_type {
    PARTAKE_SHMEM_UNKNOWN,
    PARTAKE_SHMEM_MMAP,
    PARTAKE_SHMEM_SHMGET,
    PARTAKE_SHMEM_WIN32,
};


struct partake_daemon_config {
    TCHAR *socket;

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
