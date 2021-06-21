# Copyright 2020-2021 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

partaked_sources = [
    'main.c',
    'partaked_allocator.c',
    'partaked_channel.c',
    'partaked_connection.c',
    'partaked_daemon.c',
    'partaked_handle.c',
    'partaked_iobuf.c',
    'partaked_malloc.c',
    'partaked_pool.c',
    'partaked_random.c',
    'partaked_request.c',
    'partaked_response.c',
    'partaked_segment.c',
    'partaked_sender.c',
    'partaked_shmem_mmap.c',
    'partaked_shmem_shmget.c',
    'partaked_shmem_win32.c',
    'partaked_task.c',
    'partaked_token.c',
    'partaked_voucherqueue.c',
]

executable('partaked',
    sources: [
        common_sources,
        partaked_sources,
        protocol_headers,
        dropt_sources,
        zf_log_sources,
    ],
    include_directories: [
        common_incdir,
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
    gnu_symbol_visibility: 'hidden',
)
