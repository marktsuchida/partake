/*
 * Partake object tokens
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <string.h>


// A token is a key used by partaked to locate an object. To clients it is an
// opaque byte string. Within the lifetime of a partaked instance, tokens are
// unique and never reused; for STANDARD policy objects, tokens uniquely
// identifies published object content. (There are enough unique 64-bit numbers
// that we will never loop around.) The null (zero) token is not used.
typedef uint64_t partake_token;


extern partake_token partake_prev_token;


// Any non-zero value would work for this.
#define PARTAKE_TOKEN_SEED 0xffffffffffffffffULL


static inline partake_token partake_generate_token(void) {
    // Tokens are generated using a pseudorandom sequence that will emit
    // 2^64 - 1 different non-zero values before looping around. (Sequential
    // numbers would also work, but this prevents users from making assumptions
    // about token values (unless they are determined to). It also ensures good
    // properties as a hash table key.)

    uint64_t t = partake_prev_token;

    // See https://en.wikipedia.org/wiki/Xorshift
    t ^= t << 13;
    t ^= t >> 7;
    t ^= t << 17;

    partake_prev_token = t;
    return t;
}
