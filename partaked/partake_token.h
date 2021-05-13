/*
 * Partake object tokens
 *
 *
 * Copyright (C) 2020, The Board of Regents of the University of Wisconsin
 * System
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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


static inline partake_token partake_generate_token() {
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
