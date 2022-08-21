project(
    'dropt',
    'c',
    version: '2.0.1',
)

includes = include_directories('include')

install_headers(
    [
        'include/dropt.h',
        'include/dropt_string.h',
        'include/droptxx.hpp',
    ],
)

dropt_sources = [
    'src/dropt.c',
    'src/dropt_handlers.c',
    'src/dropt_string.c',
]

dropt_lib = library(
    'dropt',
    dropt_sources,
    include_directories: includes,
    install: true,
)

dropt_dep = declare_dependency(
    include_directories: includes,
    link_with: dropt_lib,
)
