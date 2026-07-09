# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

daemon_tests_sources = files([
    'allocator.cpp',
    'cli.cpp',
    'client.cpp',
    'config.cpp',
    'connection_acceptor.cpp',
    'daemon.cpp',
    'handle.cpp',
    'handle_list.cpp',
    'hive.cpp',
    'key_sequence.cpp',
    'object.cpp',
    'page_size.cpp',
    'proper_object.cpp',
    'quitter.cpp',
    'repository.cpp',
    'request_handler.cpp',
    'response_builder.cpp',
    'segment.cpp',
    'session.cpp',
    'shmem_mmap.cpp',
    'shmem_sysv.cpp',
    'shmem_win32.cpp',
    'sizes.cpp',
    'time_point.cpp',
    'token_hash_table.cpp',
    'voucher.cpp',
    'voucher_queue.cpp',
])

daemon_tests = executable(
    'daemon_tests',
    [
        tests_main,
        daemon_tests_sources,
        common_sources,
        daemon_sources,
        protocol_cpp_headers,
    ],
    include_directories: [common_incdir, common_tests_incdir, daemon_incdir],
    dependencies: daemon_deps + [catch2_dep, trompeloeil_dep],
    cpp_args: ['-DBOOST_ASIO_ENABLE_HANDLER_TRACKING'],
)
test('daemon tests', daemon_tests)
