/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "random.hpp"

#include <algorithm>
#include <random>
#include <string_view>

namespace partake::common {

namespace {

auto randev() -> auto & {
    static std::random_device rd;
    return rd;
}

} // namespace

auto random_string(std::size_t len) -> std::string {
    static constexpr std::string_view letters = "0123456789"
                                                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                "abcdefghijklmnopqrstuvwxyz";
    auto &rd = randev();
    auto distrib =
        std::uniform_int_distribution<std::size_t>(0, letters.size() - 1);
    std::string ret(len, '\0');
    std::generate(ret.begin(), ret.end(),
                  [&]() { return letters[distrib(rd)]; });
    return ret;
}

} // namespace partake::common
