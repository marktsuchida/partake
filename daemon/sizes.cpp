/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "sizes.hpp"

#include <fmt/core.h>
#include <spdlog/spdlog.h>

namespace partake::daemon {

auto round_up_or_check_size(std::size_t &size, std::size_t granularity)
    -> bool {
    // Do not automatically round up to granularity >= 1 MiB, so as not to
    // accidentally allocate unexpectedly large blocks.
    static constexpr std::size_t threshold = 1u << 20;
    if (granularity == 0) {
        spdlog::error("Could not determine correct allocation granularity");
        return false;
    }
    auto div = size / granularity;
    auto mod = size % granularity;
    if (mod == 0)
        return true;
    if (granularity < threshold) {
        auto const rounded_up = (div + 1) * granularity;
        spdlog::warn("Requested size ({}) rounded up to {}",
                     human_readable_size(size),
                     human_readable_size(rounded_up));
        size = rounded_up;
        return true;
    }
    spdlog::error(
        "Requested size ({}) is not a multiple of the required granularity ({})",
        human_readable_size(size), human_readable_size(granularity));
    spdlog::info(
        "Automatic round up is disabled when the required granularity is >= {}",
        human_readable_size(threshold));
    return false;
}

auto human_readable_size(std::size_t size) -> std::string {
    // Do not round numbers; only summarize when exact.
    static constexpr auto kibishift = 10;
    static constexpr std::size_t sub_kibi_mask = (1u << kibishift) - 1;
    if (size == 0)
        return "0 bytes";
    if (size == 1)
        return "1 byte";
    if ((size & sub_kibi_mask) != 0u)
        return fmt::format("{} bytes", size);
    size >>= kibishift;
    if ((size & sub_kibi_mask) != 0u)
        return fmt::format("{} KiB", size);
    size >>= kibishift;
    if ((size & sub_kibi_mask) != 0u)
        return fmt::format("{} MiB", size);
    size >>= kibishift;
    if ((size & sub_kibi_mask) != 0u)
        return fmt::format("{} GiB", size);
    size >>= kibishift;
    if ((size & sub_kibi_mask) != 0u)
        return fmt::format("{} TiB", size);
    size >>= kibishift;
    if ((size & sub_kibi_mask) != 0u)
        return fmt::format("{} PiB", size);
    size >>= kibishift;
    return fmt::format("{} EiB", size);
}

} // namespace partake::daemon
