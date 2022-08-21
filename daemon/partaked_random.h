/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stddef.h>

int partaked_generate_random_int(void);

char *partaked_alloc_random_name(char *prefix, size_t random_len,
                                 size_t max_total_len);
