/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <string>

namespace partake::daemon {

// Round 'size' up to a multiple of 'granularity' and return true if
// granularity is small. If granularity is large, return true if size is
// already a multiple of granularity; otherwise return false. Return false if
// granularity is zero.
auto round_up_or_check_size(std::size_t &size,
                            std::size_t granularity) noexcept -> bool;

auto human_readable_size(std::size_t size) noexcept -> std::string;

// 'size' must not be zero.
inline auto is_size_power_of_2(std::size_t size) noexcept -> bool {
    assert(size > 0);
    return (size & (size - 1)) == 0;
}

// 'size' must be a power of 2.
inline auto log2_size(std::size_t size) noexcept -> std::size_t {
    assert(size > 0);
    assert(is_size_power_of_2(size));
    std::size_t ret = 0;
    while (size != 1) {
        size >>= 1;
        ++ret;
    }
    return ret;
}

} // namespace partake::daemon
