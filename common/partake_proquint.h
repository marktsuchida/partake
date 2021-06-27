/*
 * An implementation of proquint (https://arxiv.org/html/0901.4016)
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * In partake, object (and voucher) tokens are 64-bit values. Although they are
 * handled as binary data internally and in the FlatBuffers protocol, they are
 * displayed in "proquint" representation for logging and debugging purposes.
 * These human-pronounceable strings are easier to identify than 16 hex digits.
 *
 * Although there is an existing implementation
 * (http://github.com/dsw/proquint), this one performs validation on input and
 * also supports 64-bit integers.
 *
 * The proquint spec does not discuss byte order, but clearly converts 32-bit
 * examples in msb-to-lsb order. We do the same here.
 */

#pragma once

#include <stdint.h>

void partake_proquint_from_uint64(uint64_t i, char *dest28);

int partake_proquint_to_uint64(const char *pq, uint64_t *dest);
