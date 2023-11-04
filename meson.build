# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

project('partake', 'cpp',
    version: '0.1.0-dev',
    license: 'MIT',
    meson_version: '>=0.56.0',
    default_options: [
        'cpp_std=c++17',
        'werror=true',
        'warning_level=3',
    ],
)

add_project_arguments(
    '-DPARTAKE_VERSION=' + meson.project_version(),
    language: 'cpp',
)

cxx = meson.get_compiler('cpp')

if cxx.get_id() in ['msvc', 'clang-cl']
    add_global_arguments('/utf-8', language: 'cpp')
    add_project_arguments('-DNOMINMAX', language: 'cpp')
    add_global_arguments('-D_WIN32_WINNT=0x0A00', language: 'cpp') # Windows 10
endif

if cxx.get_id() in ['gcc', 'clang']
    # Extra warnings
    add_project_arguments(['-Wshadow', '-Wconversion'], language: 'cpp')
endif

if host_machine.system() == 'darwin'
    # macOS has the libunwind functions always available in libSystem
    add_project_arguments(
        '-DBOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED',
        language: 'cpp',
    )
endif

boost_dep = dependency('boost', include_type: 'system')

gsl_dep = dependency(
    'gsl',
    fallback: ['microsoft-gsl', 'microsoft_gsl_dep'],
    include_type: 'system',
)

expected_dep = dependency(
    'tl-expected',
    fallback: 'tl-expected',
    include_type: 'system',
)

fmt_dep = dependency('fmt', fallback: 'fmt', include_type: 'system')

cli11_dep = dependency('cli11', fallback: 'cli11', include_type: 'system')

spdlog_dep = dependency('spdlog', fallback: 'spdlog', include_type: 'system')

plf_colony_dep = dependency(
    'plf_colony',
    fallback: 'plf_colony',
    include_type: 'system',
)

flatbuffers_dep = dependency(
    'flatbuffers',
    fallback: 'flatbuffers',
    include_type: 'system',
)
flatc_prog = find_program('flatc', required: false)
if not flatc_prog.found()
    flatc_prog = subproject('flatbuffers').get_variable('flatc')
endif

# POSIX shm_open() and shm_unlink()
librt = cxx.find_library('rt', required: false)

doctest_dep = dependency(
    'doctest',
    fallback: 'doctest',
    include_type: 'system',
)

trompeloeil_dep = dependency(
    'trompeloeil',
    fallback: ['trompeloeil', 'trompeloeil_dep'],
    include_type: 'system',
)

subdir('common')
subdir('protocol')
subdir('daemon')
