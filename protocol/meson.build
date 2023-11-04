# This file is part of the partake project
# Copyright 2020-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

protocol_cpp_headers = custom_target(
    'protocol_cpp_headers',
    output: 'partake_protocol_generated.h',
    input: 'partake_protocol.fbs',
    command: [
        flatc_prog,
        '--cpp',
        '--cpp-std=c++17',
        '--scoped-enums',
        '-o', '@OUTDIR@',
        '@INPUT@',
    ],
)
