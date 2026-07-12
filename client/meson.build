# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

client_incdir = include_directories('include')
client_src_incdir = include_directories('src')

threads_dep = dependency('threads')

client_lib = library(
    'partake-client',
    [
        files([
            'src/client.cpp',
            'src/connection.cpp',
            'src/errors.cpp',
            'src/event.cpp',
            'src/objview.cpp',
            'src/queue.cpp',
            'src/token.cpp',
        ]),
        common_sources,
    ],
    include_directories: [client_incdir, client_src_incdir, common_incdir],
    dependencies: [boost_dep, fmt_dep, gsl_dep, spdlog_dep, threads_dep],
    override_options: ['cpp_std=c++20'],
)

client_dep = declare_dependency(
    link_with: client_lib,
    include_directories: client_incdir,
)

subdir('tests')
