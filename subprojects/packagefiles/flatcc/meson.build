# This file is part of the partake project
# Copyright 2020-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

project(
    'flatcc',
    'c',
    version: '0.6.1',
)

c_args = ['-DFLATCC_REFLECTION=1']

public_includes = include_directories('include')

includes = include_directories(['include', 'config', 'external'])

install_headers(
    [
        'include/flatcc/flatcc.h',
        'include/flatcc/flatcc_accessors.h',
        'include/flatcc/flatcc_alloc.h',
        'include/flatcc/flatcc_assert.h',
        'include/flatcc/flatcc_builder.h',
        'include/flatcc/flatcc_emitter.h',
        'include/flatcc/flatcc_endian.h',
        'include/flatcc/flatcc_epilogue.h',
        'include/flatcc/flatcc_flatbuffers.h',
        'include/flatcc/flatcc_identifier.h',
        'include/flatcc/flatcc_iov.h',
        'include/flatcc/flatcc_json_parser.h',
        'include/flatcc/flatcc_json_printer.h',
        'include/flatcc/flatcc_portable.h',
        'include/flatcc/flatcc_prologue.h',
        'include/flatcc/flatcc_refmap.h',
        'include/flatcc/flatcc_rtconfig.h',
        'include/flatcc/flatcc_types.h',
        'include/flatcc/flatcc_unaligned.h',
        'include/flatcc/flatcc_verifier.h',
        'include/flatcc/flatcc_version.h',
    ],
    subdir: 'flatcc',
)

flatccrt_lib_sources = [
    'src/runtime/builder.c',
    'src/runtime/emitter.c',
    'src/runtime/json_parser.c',
    'src/runtime/json_printer.c',
    'src/runtime/refmap.c',
    'src/runtime/verifier.c',
]

flatcc_lib_sources = [
    'src/compiler/codegen_c.c',
    'src/compiler/codegen_c_builder.c',
    'src/compiler/codegen_c_json_parser.c',
    'src/compiler/codegen_c_json_printer.c',
    'src/compiler/codegen_c_reader.c',
    'src/compiler/codegen_c_sort.c',
    'src/compiler/codegen_c_sorter.c',
    'src/compiler/codegen_c_verifier.c',
    'src/compiler/codegen_schema.c',
    'src/compiler/coerce.c',
    'src/compiler/fileio.c',
    'src/compiler/flatcc.c',
    'src/compiler/hash_tables/name_table.c',
    'src/compiler/hash_tables/schema_table.c',
    'src/compiler/hash_tables/scope_table.c',
    'src/compiler/hash_tables/symbol_table.c',
    'src/compiler/hash_tables/value_set.c',
    'src/compiler/parser.c',
    'src/compiler/semantics.c',
    'external/hash/cmetrohash64.c',
    'external/hash/ptr_set.c',
    'external/hash/str_set.c',
]

flatcc_cli_sources = [
    'src/cli/flatcc_cli.c',
]

flatccrt_lib = library(
    'flatccrt',
    flatccrt_lib_sources,
    include_directories: includes,
    c_args: c_args,
    install: true,
)

flatcc_lib = library(
    'flatcc',
    [flatcc_lib_sources, flatccrt_lib_sources],
    include_directories: includes,
    c_args: c_args,
    install: true,
)

flatcc_prog = executable(
    'flatcc',
    flatcc_cli_sources,
    include_directories: includes,
    link_with: flatcc_lib,
    c_args: c_args,
    install: true,
)

flatccrt_dep = declare_dependency(
    include_directories: public_includes,
    link_with: flatccrt_lib,
)
