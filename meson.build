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

# Would be nice to make libuv a subproject, but it only builds with GYP on
# Windows. For now it is easier to require a separate build.
libuv = cc.find_library('uv', required: true)

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
