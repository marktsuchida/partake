# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

common_incdir = include_directories('.')

common_sources = files([
    'asio.cpp',
    'proquint.cpp',
])

common_test = executable(
    'common_test',
    [
        'test_main.cpp',
        common_sources,
    ],
    dependencies: [
        boost_dep,
        doctest_dep,
        gsl_dep,
    ],
)
test('common test', common_test)
