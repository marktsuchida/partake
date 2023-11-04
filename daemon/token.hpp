/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <doctest.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <utility>

namespace partake::daemon {

// Data type used for object and voucher keys. To clients it is an opaque byte
// string.
class token {
    std::uint64_t t = 0;

  public:
    token() noexcept = default;
    explicit token(std::uint64_t value) noexcept : t(value) {}

    [[nodiscard]] auto as_u64() const noexcept -> std::uint64_t { return t; }

    [[nodiscard]] auto is_valid() const noexcept -> bool { return t != 0; }

    friend auto operator==(token lhs, token rhs) noexcept -> bool {
        return lhs.t == rhs.t;
    }

    friend auto operator!=(token lhs, token rhs) noexcept -> bool {
        return lhs.t != rhs.t;
    }
};

// Within the lifetime of a partaked instance, tokens are unique and never
// reused; for DEFAULT policy objects, tokens uniquely identify shared object
// content. (There are enough unique 64-bit numbers that we will never loop
// around.) The null (zero) token is not used.
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

    [[nodiscard]] auto generate() noexcept -> token {
        auto t = prev;
        assert(t != 0);

        // See https://en.wikipedia.org/wiki/Xorshift
        t ^= t << 13;
        t ^= t >> 7;
        t ^= t << 17;

        prev = t;
        return token(t);
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

namespace std {

// We use token value as its own hash, because it is already randomized.
template <> struct hash<partake::daemon::token> {
    auto operator()(partake::daemon::token tok) const noexcept -> size_t {
        auto const t = tok.as_u64();
        static_assert(sizeof(size_t) == 4 || sizeof(size_t) == 8);
        if constexpr (sizeof(size_t) == 8) {
            return t;
        } else {
            return static_cast<size_t>(t >> 32) ^ static_cast<size_t>(t);
        }
    }
};

} // namespace std
