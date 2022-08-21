# This file is part of the partake project
# Copyright 2020-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

project(
    'zf_log',
    'c',
    version: '0.4.0',
)

includes = include_directories('zf_log')

install_headers(
    ['zf_log/zf_log.h'],
)

zf_log_sources = [
    'zf_log/zf_log.c',
]

zf_log_lib = library(
    'zf_log',
    zf_log_sources,
    include_directories: includes,
    install: true,
)

zf_log_dep = declare_dependency(
    include_directories: includes,
    link_with: zf_log_lib,
)
