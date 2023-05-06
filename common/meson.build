# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

common_incdir = include_directories('.')

common_sources = files([
    'proquint.cpp',
])

common_test = executable(
    'common_test',
    [
        common_sources,
        'test_main.cpp',
    ],
    dependencies: [
        doctest_dep,
        gsl_dep,
    ],
)
test('common test', common_test)
