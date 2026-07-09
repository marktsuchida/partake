/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "sizes.hpp"

#include <catch2/catch_test_macros.hpp>

namespace partake::daemon {

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
