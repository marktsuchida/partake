# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

daemon_incdir = include_directories('.')

daemon_sources = files([
    'cli.cpp',
    'page_size.cpp',
    'segment.cpp',
    'shmem_mmap.cpp',
    'shmem_sysv.cpp',
    'shmem_win32.cpp',
    'sizes.cpp',
])

daemon_deps = [
    boost_dep,
    cli11_dep,
    expected_dep,
    flatbuffers_dep,
    fmt_dep,
    gsl_dep,
    librt,
    plf_colony_dep,
    spdlog_dep,
]

executable('partaked',
    sources: [
        'main.cpp',
        common_sources,
        daemon_sources,
        protocol_cpp_headers,
    ],
    include_directories: [common_incdir],
    dependencies: daemon_deps,
    gnu_symbol_visibility: 'hidden',
)

subdir('tests')
