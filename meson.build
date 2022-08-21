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

libflatccrt = cc.find_library('flatccrt', required: false)
prog_flatcc = find_program('flatcc', required: false)

if not libflatccrt.found() or not prog_flatcc.found()
    # Note that we use the 'meson' branch of flatcc
    flatcc_proj = subproject('flatcc')

    libflatccrt = declare_dependency(
        include_directories: flatcc_proj.get_variable('inc_dir'),
        link_with: flatcc_proj.get_variable('libflatccrt'),
    )

    prog_flatcc = flatcc_proj.get_variable('flatcc')
endif

uthash_dep = dependency('uthash', fallback: 'uthash')

unity_dep = dependency('unity', fallback: 'unity')

subdir('thirdparty')
subdir('common')
subdir('protocol')
subdir('daemon')
