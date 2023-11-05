/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "sizes.hpp"

#include <doctest.h>
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

TEST_CASE("round_up_or_check_size") {
    std::size_t size = 0;
    CHECK(round_up_or_check_size(size, 4096));
    CHECK(size == 0);
    size = 1;
    CHECK(round_up_or_check_size(size, 4096));
    CHECK(size == 4096);
    CHECK(round_up_or_check_size(size, 4096));
    CHECK(size == 4096);

    size = 0;
    CHECK(round_up_or_check_size(size, 1048576));
    CHECK(size == 0);
    size = 1;
    CHECK_FALSE(round_up_or_check_size(size, 1048576));
    CHECK(size == 1);
    size = 1048576;
    CHECK(round_up_or_check_size(size, 1048576));
    CHECK(size == 1048576);
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

TEST_CASE("human_readable_size") {
    CHECK(human_readable_size(0) == "0 bytes");
    CHECK(human_readable_size(1) == "1 byte");
    CHECK(human_readable_size(2) == "2 bytes");
    CHECK(human_readable_size(1023) == "1023 bytes");
    CHECK(human_readable_size(1024) == "1 KiB");
    CHECK(human_readable_size(1025) == "1025 bytes");
    CHECK(human_readable_size(1u << 20) == "1 MiB");
    CHECK(human_readable_size(1u << 30) == "1 GiB");
}

TEST_CASE("is_size_power_of_2") {
    CHECK(is_size_power_of_2(1));
    CHECK(is_size_power_of_2(2));
    CHECK_FALSE(is_size_power_of_2(3));
    CHECK(is_size_power_of_2(4));
    CHECK_FALSE(is_size_power_of_2(511));
    CHECK(is_size_power_of_2(512));
    CHECK_FALSE(is_size_power_of_2(513));
}

TEST_CASE("log2_size") {
    CHECK(log2_size(1) == 0);
    CHECK(log2_size(2) == 1);
    CHECK(log2_size(4) == 2);
    CHECK(log2_size(1024) == 10);
}

} // namespace partake::daemon
