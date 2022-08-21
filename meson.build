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

libuv = dependency('libuv', fallback: 'libuv')

libflatccrt = dependency('flatccrt', fallback: ['flatcc', 'flatccrt_dep'])
prog_flatcc = find_program('flatcc', required: false)
if not prog_flatcc.found()
    prog_flatcc = subproject('flatcc').get_variable('flatcc_prog')
endif

uthash_dep = dependency('uthash', fallback: 'uthash')

unity_dep = dependency('unity', fallback: 'unity')

subdir('thirdparty')
subdir('common')
subdir('protocol')
subdir('daemon')
