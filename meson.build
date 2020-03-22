# Copyright (C) 2020, The Board of Regents of the University of Wisconsin
# System
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

project('partake', 'c')

cc = meson.get_compiler('c')

# POSIX shm_open() and shm_unlink()
librt = cc.find_library('rt', required: false)

libbcrypt = cc.find_library('bcrypt', required: false)

libuv = dependency('uv', required: false)

if not libuv.found()
    cmake = import('cmake')

    libuv_needs_win32_libs = host_machine.system() == 'windows'
    winsock = cc.find_library('Ws2_32', required: libuv_needs_win32_libs)
    iphlpapi = cc.find_library('Iphlpapi', required: libuv_needs_win32_libs)
    userenv = cc.find_library('Userenv', required: libuv_needs_win32_libs)

    libuv_proj = cmake.subproject('libuv')

    libuv = declare_dependency(
        include_directories: libuv_proj.include_directories('uv_a'),
        link_with: libuv_proj.get_variable('uv_a'),
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

subdir('thirdparty')
subdir('protocol')
subdir('partaked')
