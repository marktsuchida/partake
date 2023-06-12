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

// A token is a key used by partaked to locate an object. To clients it is an
// opaque byte string. Within the lifetime of a partaked instance, tokens are
// unique and never reused; for DEFAULT policy objects, tokens uniquely
// identify shared object content. (There are enough unique 64-bit numbers
// that we will never loop around.) The null (zero) token is not used.
// The name 'btoken' stands for 'binary token' (as opposed to proquint
// representation). It also avoid clashing with methods and variables named
// 'token'.
class btoken {
    std::uint64_t t = 0;

  public:
    btoken() noexcept = default;
    explicit btoken(std::uint64_t value) noexcept : t(value) {}

    [[nodiscard]] auto as_u64() const noexcept -> std::uint64_t { return t; }

    [[nodiscard]] auto is_valid() const noexcept -> bool { return t != 0; }

    friend auto operator==(btoken lhs, btoken rhs) noexcept -> bool {
        return lhs.t == rhs.t;
    }

    friend auto operator!=(btoken lhs, btoken rhs) noexcept -> bool {
        return lhs.t != rhs.t;
    }
};

// Tokens are generated using a pseudorandom sequence that will emit 2^64 - 1
// _distinct_ non-zero values before looping around. (Sequential numbers would
// also work, but we don't want to tempt users to make assumptions about token
// values (unless they are determined to). The pseudorandom tokens also serve
// as good hash table keys.)
class token_sequence {
    std::uint64_t prev_token = 0xffff'ffff'ffff'ffffuLL;

  public:
    token_sequence() noexcept = default;

    // Copying suggests a bug, so allow move only. Moved-from object is not
    // usable.
    token_sequence(token_sequence &&other) noexcept
        : prev_token(std::exchange(other.prev_token, 0uLL)) {}

    auto operator=(token_sequence &&rhs) noexcept -> token_sequence & {
        prev_token = std::exchange(rhs.prev_token, 0uLL);
        return *this;
    }

    [[nodiscard]] auto generate() noexcept -> btoken {
        auto t = prev_token;
        assert(t != 0);

        // See https://en.wikipedia.org/wiki/Xorshift
        t ^= t << 13;
        t ^= t >> 7;
        t ^= t << 17;

        prev_token = t;
        return btoken(t);
    }
};

TEST_CASE("token_sequence") {
    // Smoke test only.
    token_sequence seq;
    CHECK(~seq.generate().as_u64() != 0);
    CHECK(seq.generate().is_valid());
    CHECK(seq.generate() != seq.generate());
}

} // namespace partake::daemon

namespace std {

// We use btoken value as its own hash, because it is already randomized.
template <> struct hash<partake::daemon::btoken> {
    auto operator()(partake::daemon::btoken tok) const noexcept -> size_t {
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
