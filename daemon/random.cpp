/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "random.hpp"

#include <doctest.h>

#include <algorithm>
#include <cctype>
#include <random>
#include <string_view>

namespace partake::daemon {

namespace {

auto randev() noexcept -> auto & {
    static std::random_device rd;
    return rd;
}

} // namespace

auto random_string(std::size_t len) noexcept -> std::string {
    static constexpr std::string_view letters = "0123456789"
                                                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                "abcdefghijklmnopqrstuvwxyz";
    auto &rd = randev();
    auto distrib =
        std::uniform_int_distribution<std::size_t>(0, letters.size() - 1);
    std::string ret(len, '\0');
    std::generate(ret.begin(), ret.end(),
                  [&]() noexcept { return letters[distrib(rd)]; });
    return ret;
}

TEST_CASE("random_string") {
    CHECK(random_string(0).empty());

    auto const r1 = random_string(1);
    CHECK(r1.size() == 1);
    CHECK(std::isalnum(r1.front()));

    auto const r237 = random_string(237);
    CHECK(r237.size() == 237);
    CHECK(std::all_of(r237.begin(), r237.end(),
                      [](char c) noexcept { return std::isalnum(c); }));
}

} // namespace partake::daemon
