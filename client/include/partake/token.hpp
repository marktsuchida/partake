/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace partake::client {

// Object or voucher key; an opaque, trivially copyable value type.
class token {
    std::uint64_t t = 0;

  public:
    token() noexcept = default; // Invalid token.
    explicit token(std::uint64_t value) noexcept : t(value) {}

    [[nodiscard]] auto as_u64() const noexcept -> std::uint64_t { return t; }

    [[nodiscard]] auto is_valid() const noexcept -> bool { return t != 0; }

    // Human-pronounceable spelling, e.g. "lusab-babad-gutih-tugad".
    [[nodiscard]] auto to_proquint() const -> std::string;

    // Returns an invalid token on any parse failure; callers should check
    // is_valid().
    [[nodiscard]] static auto from_proquint(std::string_view s) -> token;

    [[nodiscard]] friend auto operator==(token lhs, token rhs) noexcept
        -> bool {
        return lhs.t == rhs.t;
    }

    [[nodiscard]] friend auto operator!=(token lhs, token rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }
};

} // namespace partake::client

namespace std {

// We use the token value as its own hash, because it is already randomized.
template <> struct hash<partake::client::token> {
    auto operator()(partake::client::token tok) const noexcept -> size_t {
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
