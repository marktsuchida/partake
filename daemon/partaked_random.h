/*
 * Random string generation
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_tchar.h"


int partaked_generate_random_int(void);

TCHAR *partaked_alloc_random_name(TCHAR *prefix, size_t random_len,
        size_t max_total_len);
