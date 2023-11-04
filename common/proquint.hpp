/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * An implementation of proquint (https://arxiv.org/html/0901.4016)
 *
 * In partake, object (and voucher) tokens are 64-bit values. Although they are
 * handled as binary data internally and in the FlatBuffers protocol, they are
 * displayed in "proquint" representation for logging and debugging purposes.
 * These human-pronounceable strings are easier to identify than 16 hex digits.
 *
 * Although there is an existing implementation
 * (http://github.com/dsw/proquint), this one performs validation on input and
 * supports 64-bit integers.
 *
 * The proquint spec does not discuss byte order, but clearly converts 32-bit
 * examples in msb-to-lsb order. We do the same here.
 */

#include <gsl/span>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace partake::common {

namespace internal {

void proquint_from_u64(gsl::span<char, 23> dest, std::uint64_t i) noexcept;

[[nodiscard]] auto proquint_to_u64(gsl::span<char const, 23> pq) noexcept
    -> std::pair<std::uint64_t, bool>;

} // namespace internal

class proquint64 {
    std::uint64_t val;

  public:
    static constexpr std::size_t length = 5 * 4 + 3;

    constexpr proquint64() noexcept : val(0) {}

    explicit constexpr proquint64(std::uint64_t i) noexcept : val(i) {}

    // Narrow contract: pq must be valid proquint string (see validate())
    explicit proquint64(std::string_view pq)
        : val([pq] {
              assert(pq.size() == length);
              auto [v, ok] =
                  internal::proquint_to_u64(gsl::span<char const, length>(pq));
              assert(ok);
              return v;
          }()) {}

    // TODO: Avoid implicit conversions? We could provide to_string override
    // and/or accessors.
    [[nodiscard]] operator std::string() const {
        std::string ret;
        ret.resize(size());
        internal::proquint_from_u64(gsl::span<char, length>(ret), val);
        return ret;
    }

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept {
        return val;
    }

    [[nodiscard]] static auto validate(std::string_view pq)
        -> std::optional<proquint64> {
        assert(pq.data() != nullptr);
        if (pq.size() != length)
            return std::nullopt;
        auto [v, ok] =
            internal::proquint_to_u64(gsl::span<char const, length>(pq));
        if (not ok)
            return std::nullopt;
        return proquint64(v);
    }

    void write_to(gsl::span<char, length> dest) const {
        internal::proquint_from_u64(dest, val);
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
        return length;
    }
};

[[nodiscard]] inline auto operator==(proquint64 lhs, proquint64 rhs) noexcept
    -> bool {
    return std::uint64_t(lhs) == std::uint64_t(rhs);
}

[[nodiscard]] inline auto operator!=(proquint64 lhs, proquint64 rhs) noexcept
    -> bool {
    return not(lhs == rhs);
}

} // namespace partake::common
