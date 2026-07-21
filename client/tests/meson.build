# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

# Proves the public headers are complete, self-contained, C++17, and
# stdlib-only (no other include directories are provided).
cxx17_header_check = static_library(
    'client_cxx17_header_check',
    'cxx17_header_check.cpp',
    include_directories: [client_incdir],
    override_options: ['cpp_std=c++17'],
)

client_tests_sources = files([
    'client.cpp',
    'client_impl.cpp',
    'connection.cpp',
    'connection_impl.cpp',
    'errors.cpp',
    'event.cpp',
    'event_impl.cpp',
    'mock_server.cpp',
    'objview.cpp',
    'objview_impl.cpp',
    'partake.cpp',
    'queue.cpp',
    'queue_impl.cpp',
    'request_builder.cpp',
    'requests.cpp',
    'segment_cache.cpp',
    'seqno_map.cpp',
    'shmem_attach.cpp',
    'token.cpp',
    'types.cpp',
    'unique_handler.cpp',
])

# Daemon shmem sources, linked into client_tests so the attach and cache tests
# can create real in-test segments (create_posix_mmap_shmem /
# create_file_mmap_shmem / create_sysv_shmem). These are C++17-clean and build
# at the test target's C++20; random.cpp (their other dep) is already in
# common_sources via client_dep, and daemon_incdir/common_incdir are on the
# include path.
client_tests_daemon_shmem_sources = files([
    '../../daemon/page_size.cpp',
    '../../daemon/shmem_mmap.cpp',
    '../../daemon/shmem_sysv.cpp',
    '../../daemon/sizes.cpp',
])

client_tests = executable(
    'client_tests',
    [
        tests_main,
        client_tests_sources,
        client_tests_daemon_shmem_sources,
        protocol_cpp_headers,
    ],
    include_directories: [
        client_incdir,
        client_src_incdir,
        common_incdir,
        common_tests_incdir,
        daemon_incdir, # For response_builder.hpp (mock_server).
    ],
    dependencies: [
        boost_dep,
        catch2_dep,
        client_dep,
        expected_dep,
        flatbuffers_dep,
        fmt_dep,
        gsl_dep,
        spdlog_dep,
        threads_dep,
    ],
    # No -DBOOST_ASIO_ENABLE_HANDLER_TRACKING here (unlike common/daemon
    # tests): these tests link client_lib, which is compiled without it,
    # and Asio config macros must match across TUs sharing an io_context.
    override_options: ['cpp_std=c++20'],
)
test('client tests', client_tests)

# Manual end-to-end smoke test against a real partaked (public headers only);
# deliberately not registered as a test. See smoke.cpp for usage.
executable(
    'client_smoke',
    'smoke.cpp',
    dependencies: [client_dep, threads_dep],
    override_options: ['cpp_std=c++20'],
)
