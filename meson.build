# Copyright 2020-2021 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

project('partake', 'c')

cc = meson.get_compiler('c')

# POSIX shm_open() and shm_unlink()
librt = cc.find_library('rt', required: false)

# arc4random() on Linux
libbsd = cc.find_library('bsd', required: false)

libbcrypt = cc.find_library('bcrypt', required: false)

libuv = dependency('libuv', required: false) # pkg-config on Unix

if not libuv.found()
    cmake = import('cmake')

    libuv_needs_win32_libs = host_machine.system() == 'windows'
    winsock = cc.find_library('Ws2_32', required: libuv_needs_win32_libs)
    iphlpapi = cc.find_library('Iphlpapi', required: libuv_needs_win32_libs)
    userenv = cc.find_library('Userenv', required: libuv_needs_win32_libs)

    libuv_proj = cmake.subproject('libuv',
        cmake_options: [
            '-DBUILD_TESTING=NO',
        ],
    )

    libuv = declare_dependency(
        include_directories: libuv_proj.include_directories('uv_a'),
        link_with: libuv_proj.target('uv_a'),
        dependencies: [ # Needed for the static version (uv_a)
            winsock,
            iphlpapi,
            userenv,
        ],
    )
endif

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

unity_dep = dependency('unity', fallback: ['unity', 'unity_dep'])

subdir('thirdparty')
subdir('common')
subdir('protocol')
subdir('partaked')
