prog_flatcc = find_program('flatcc')

protocol_headers = custom_target('protocol_headers',
    output: [
        'flatbuffers_common_builder.h',
        'flatbuffers_common_reader.h',
        'partake_builder.h',
        'partake_reader.h',
    ],
    input: 'partake.fbs',
    command: [
        prog_flatcc, '-cwo', '@OUTDIR@', '@INPUT@',
    ],
)
