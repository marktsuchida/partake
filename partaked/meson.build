# Copyright 2020-2021 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

partaked_sources = [
    'main.c',
    'partake_allocator.c',
    'partake_channel.c',
    'partake_connection.c',
    'partake_daemon.c',
    'partake_handle.c',
    'partake_iobuf.c',
    'partake_malloc.c',
    'partake_pool.c',
    'partake_request.c',
    'partake_response.c',
    'partake_segment.c',
    'partake_sender.c',
    'partake_shmem.c',
    'partake_shmem_mmap.c',
    'partake_shmem_shmget.c',
    'partake_shmem_win32.c',
    'partake_task.c',
    'partake_token.c',
    'partake_voucherqueue.c',
]

executable('partaked',
    sources: [
        partaked_sources,
        protocol_headers,
        dropt_sources,
        zf_log_sources,
    ],
    include_directories: [
        dlmalloc_incdir,
        dropt_incdir,
        uthash_incdir,
        zf_log_incdir,
    ],
    dependencies: [
        libbcrypt,
        libbsd,
        libflatccrt,
        librt,
        libuv,
    ],
)
