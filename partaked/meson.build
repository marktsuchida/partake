partaked_sources = [
    'main.c',
]

executable('partaked',
    sources: [
        partaked_sources,
        protocol_headers,
        dropt_sources,
        zf_log_sources,
    ],
    include_directories: [
        dlmalloc_incdir,
        dropt_incdir,
        uthash_incdir,
        zf_log_incdir,
    ],
)
