# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

common_incdir = include_directories('.')

common_sources = files([
    'asio.cpp',
    'errors.cpp',
    'logging.cpp',
    'message.cpp',
    'overloaded.cpp',
    'posix.cpp',
    'proquint.cpp',
    'random.cpp',
    'testing.cpp',
    'token.cpp',
    'win32.cpp',
])

# TODO: common_test is now redundant: all these tests are also part of
# daemon_test. Just remove?

common_test = executable(
    'common_test',
    [
        'test_main.cpp',
        common_sources,
    ],
    dependencies: [
        boost_dep,
        doctest_dep,
        flatbuffers_dep,
        fmt_dep,
        gsl_dep,
        spdlog_dep,
    ],
)
test('common test', common_test)
