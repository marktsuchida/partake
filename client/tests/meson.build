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
    'partake.cpp',
    'queue.cpp',
    'queue_impl.cpp',
    'request_builder.cpp',
    'requests.cpp',
    'seqno_map.cpp',
    'token.cpp',
    'types.cpp',
    'unique_handler.cpp',
])

client_tests = executable(
    'client_tests',
    [tests_main, client_tests_sources, protocol_cpp_headers],
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
