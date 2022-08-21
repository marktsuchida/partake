# This file is part of the partake project
# Copyright 2020-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

protocol_headers = custom_target('protocol_headers',
    output: [
        'flatbuffers_common_builder.h',
        'flatbuffers_common_reader.h',
        'partake_protocol_builder.h',
        'partake_protocol_reader.h',
    ],
    input: 'partake_protocol.fbs',
    command: [
        prog_flatcc, '-cvwo', '@OUTDIR@', '@INPUT@',
    ],
)
