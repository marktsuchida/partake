/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "random.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>

namespace partake::common {

TEST_CASE("random_string") {
    CHECK(random_string(0).empty());

    auto const r1 = random_string(1);
    CHECK(r1.size() == 1);
    CHECK(std::isalnum(r1.front()));

    auto const r237 = random_string(237);
    CHECK(r237.size() == 237);
    CHECK(std::all_of(r237.begin(), r237.end(),
                      [](char c) { return std::isalnum(c); }));
}

TEST_CASE("random_nonzero_u64") {
    auto const r1 = random_nonzero_u64();
    CHECK(r1 != 0);
    CHECK(r1 != random_nonzero_u64());
}

} // namespace partake::common
