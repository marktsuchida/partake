/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "token.hpp"

#include <doctest.h>

#include <cassert>
#include <cstdint>
#include <utility>

namespace partake::daemon {

// Within the lifetime of a partaked instance, keys are unique and never
// reused; for DEFAULT policy objects, a key uniquely identifies shared object
// content. (There are enough unique 64-bit numbers that we will never loop
// around.) The null (zero) token is not used as a key.
//
// Keys are generated using a pseudorandom sequence that will emit 2^64 - 1
// _distinct_ non-zero tokens before looping around. Sequential numbers would
// also work, but we don't want to tempt users to make assumptions about token
// values (unless they are determined to). The pseudorandom tokens also serve
// as good hash table keys.
class key_sequence {
    std::uint64_t prev = 0xffff'ffff'ffff'ffffuLL;

  public:
    key_sequence() noexcept = default;

    // Copying suggests a bug, so allow move only. Moved-from object is not
    // usable.
    key_sequence(key_sequence &&other) noexcept
        : prev(std::exchange(other.prev, 0uLL)) {}

    auto operator=(key_sequence &&rhs) noexcept -> key_sequence & {
        prev = std::exchange(rhs.prev, 0uLL);
        return *this;
    }

    [[nodiscard]] auto generate() noexcept -> common::token {
        auto t = prev;
        assert(t != 0);

        // See https://en.wikipedia.org/wiki/Xorshift
        t ^= t << 13;
        t ^= t >> 7;
        t ^= t << 17;

        prev = t;
        return common::token(t);
    }
};

TEST_CASE("key_sequence") {
    // Smoke test only.
    key_sequence seq;
    CHECK(~seq.generate().as_u64() != 0);
    CHECK(seq.generate().is_valid());
    CHECK(seq.generate() != seq.generate());
}

} // namespace partake::daemon
