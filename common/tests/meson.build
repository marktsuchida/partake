# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

common_tests_incdir = include_directories('.')

tests_main = files('main.cpp')

common_tests_sources = files([
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

common_tests = executable(
    'common_tests',
    [tests_main, common_tests_sources, common_sources],
    include_directories: [common_incdir],
    dependencies: [
        boost_dep,
        catch2_dep,
        flatbuffers_dep,
        fmt_dep,
        gsl_dep,
        spdlog_dep,
    ],
    cpp_args: ['-DBOOST_ASIO_ENABLE_HANDLER_TRACKING'],
)
test('common tests', common_tests)
