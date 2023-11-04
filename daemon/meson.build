# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

daemon_sources = [
    'allocator.cpp',
    'cli.cpp',
    'client.cpp',
    'connection_acceptor.cpp',
    'daemon.cpp',
    'handle.cpp',
    'handle_list.cpp',
    'hive.cpp',
    'key_sequence.cpp',
    'object.cpp',
    'overloaded.cpp',
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
    'token.cpp',
    'token_hash_table.cpp',
    'voucher.cpp',
    'voucher_queue.cpp',
]

daemon_deps = [
    boost_dep,
    cli11_dep,
    doctest_dep,
    expected_dep,
    flatbuffers_dep,
    fmt_dep,
    gsl_dep,
    librt,
    plf_colony_dep,
    spdlog_dep,
    trompeloeil_dep,
]

daemon_test = executable(
    'daemon_test',
    [
        'test_main.cpp',
        common_sources,
        daemon_sources,
        protocol_cpp_headers,
    ],
    include_directories: [
        common_incdir,
    ],
    dependencies: daemon_deps,
)
test('daemon test', daemon_test)

executable('partaked',
    sources: [
        'main.cpp',
        common_sources,
        daemon_sources,
        protocol_cpp_headers,
    ],
    include_directories: [
        common_incdir,
    ],
    cpp_args: [
        '-DDOCTEST_CONFIG_DISABLE',
        # See https://github.com/doctest/doctest/issues/691
        '-DDOCTEST_CONFIG_ASSERTS_RETURN_VALUES',
        '-DDOCTEST_CONFIG_EVALUATE_ASSERTS_EVEN_WHEN_DISABLED'
    ],
    dependencies: daemon_deps,
    gnu_symbol_visibility: 'hidden',
)
