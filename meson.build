# Copyright 2020-2021 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

project('partake', 'c',
    license: 'BSD-2-Clause',
    meson_version: '>=0.56.0',
)

cc = meson.get_compiler('c')

if cc.get_id() in ['msvc', 'clang-cl']
    add_global_arguments('/utf-8', language: 'c')
endif

# POSIX shm_open() and shm_unlink()
librt = cc.find_library('rt', required: false)

# arc4random() on Linux
libbsd = cc.find_library('bsd', required: false)

libbcrypt = cc.find_library('bcrypt', required: false)

libuv = dependency(
    'libuv',
    fallback: 'libuv',
    default_options: [
        'build_tests=false',
        'build_benchmarks=false',
    ],
)

dlmalloc_incdir = subproject('dlmalloc').get_variable('dlmalloc_incdir')

dropt_dep = dependency('dropt', fallback: 'dropt')

libflatccrt = dependency('flatccrt', fallback: ['flatcc', 'flatccrt_dep'])
prog_flatcc = find_program('flatcc', required: false)
if not prog_flatcc.found()
    prog_flatcc = subproject('flatcc').get_variable('flatcc_prog')
endif

uthash_dep = dependency('uthash', fallback: 'uthash')

zf_log_dep = dependency('zf_log', fallback: 'zf_log')

unity_dep = dependency('unity', fallback: 'unity')

subdir('common')
subdir('protocol')
subdir('daemon')
