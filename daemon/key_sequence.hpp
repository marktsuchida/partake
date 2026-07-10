/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "token.hpp"

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
// _distinct_ non-zero tokens before looping around; any nonzero seed picks a
// starting point on that single cycle, so uniqueness within an instance holds
// for every seed. The daemon seeds each instance randomly, so that sequences
// differ across instances and restarts and a stale key from a previous
// instance is not certain to collide with the new instance's keys. The
// pseudorandom values also discourage users from making assumptions about
// token values (unless they are determined to) and serve as good hash table
// keys.
class key_sequence {
    std::uint64_t prev;

  public:
    explicit key_sequence(std::uint64_t seed) noexcept : prev(seed) {
        assert(seed != 0);
    }

    ~key_sequence() = default;

    // Copying suggests a bug, so allow move only.
    key_sequence(key_sequence const &) = delete;
    auto operator=(key_sequence const &) = delete;

    key_sequence(key_sequence &&other) noexcept
        : prev(std::exchange(other.prev, 0uLL)) {}

    auto operator=(key_sequence &&rhs) noexcept -> key_sequence & {
        prev = std::exchange(rhs.prev, 0uLL);
        return *this;
    }

    [[nodiscard]] auto generate() noexcept -> common::token {
        auto t = prev;
        assert(t != 0);

        // NOLINTBEGIN(readability-magic-numbers)
        // See https://en.wikipedia.org/wiki/Xorshift
        t ^= t << 13;
        t ^= t >> 7;
        t ^= t << 17;
        // NOLINTEND(readability-magic-numbers)

        prev = t;
        return common::token(t);
    }
};

} // namespace partake::daemon
