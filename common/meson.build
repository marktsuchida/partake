# This file is part of the partake project
# Copyright 2020-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

common_incdir = include_directories('.')

common_sources = files([
    'partake_proquint.c',
])

proquint_test = executable('test_proquint', 'test_proquint.c',
    dependencies: [
        unity_dep,
    ])
test('proquint test', proquint_test)
