# Copyright 2020-2021 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: BSD-2-Clause

# We do not actually build these projects here; just define source and include
# directories.


# We use dlmalloc by #including the .c file
dlmalloc_incdir = include_directories('dlmalloc')


dropt_incdir = include_directories('dropt/include')
dropt_sources = files([
    'dropt/src/dropt.c',
    'dropt/src/dropt_handlers.c',
    'dropt/src/dropt_string.c',
])


# uthash is a header-only library
uthash_incdir = include_directories('uthash/src')


zf_log_incdir = include_directories('zf_log/zf_log')
zf_log_sources = files([
    'zf_log/zf_log/zf_log.c',
])
