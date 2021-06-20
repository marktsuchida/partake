/*
 * Definitions to be included first in all source files
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define uthash_malloc(s) partaked_malloc(s)
#define uthash_free(p, s) partaked_free(p)
