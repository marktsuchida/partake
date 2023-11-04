/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>

namespace partake::common {

// Data type used for object and voucher keys. To clients it is an opaque byte
// string.
class token {
    std::uint64_t t = 0;

  public:
    token() noexcept = default;
    explicit token(std::uint64_t value) noexcept : t(value) {}

    [[nodiscard]] auto as_u64() const noexcept -> std::uint64_t { return t; }

    [[nodiscard]] auto is_valid() const noexcept -> bool { return t != 0; }

    [[nodiscard]] friend auto operator==(token lhs, token rhs) noexcept
        -> bool {
        return lhs.t == rhs.t;
    }

    [[nodiscard]] friend auto operator!=(token lhs, token rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }
};

} // namespace partake::common

namespace std {

// We use the token value as its own hash, because it is already randomized.
template <> struct hash<partake::common::token> {
    auto operator()(partake::common::token tok) const noexcept -> size_t {
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
