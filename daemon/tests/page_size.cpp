/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "page_size.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

namespace partake::daemon {

TEST_CASE("page_size") {
    auto const p = page_size();
    CHECK(p > 0);
    bool const is_power_of_2 = (p & (p - 1)) == 0;
    CHECK(is_power_of_2);
}

#ifdef _WIN32

TEST_CASE("system_allocation_granularity") {
    auto const g = system_allocation_granularity();
    CHECK(g >= page_size());
    CHECK(g % page_size() == 0);
    bool const is_power_of_2 = (g & (g - 1)) == 0;
    CHECK(is_power_of_2);
}

#endif

namespace internal {

TEST_CASE("read_default_huge_page_size") {
    std::istringstream strm;

    SECTION("empty file") { CHECK(read_default_huge_page_size(strm) == 0); }

    SECTION("missing key") {
        strm.str("Aaa: bbb");
        CHECK(read_default_huge_page_size(strm) == 0);
    }

    SECTION("typical") {
        strm.str("Aaa:  bbb\nHugepagesize:  1024 kB\nCcc:  ddd");
        CHECK(read_default_huge_page_size(strm) == 1048576);
    }

    SECTION("missing value") {
        strm.str("Hugepagesize:");
        CHECK(read_default_huge_page_size(strm) == 0);
    }

    SECTION("missing unit") {
        strm.str("Hugepagesize: 1024");
        CHECK(read_default_huge_page_size(strm) == 0);
    }

    SECTION("wrong unit") {
        strm.str("Hugepagesize: 1024 MB");
        CHECK(read_default_huge_page_size(strm) == 0);
    }

    SECTION("extra token") {
        strm.str("Hugepagesize: 1024 kB  blah");
        CHECK(read_default_huge_page_size(strm) == 0);
    }
}

TEST_CASE("parse_huge_page_filename") {
    CHECK(parse_huge_page_filename("") == 0);
    CHECK(parse_huge_page_filename("hugepages-xxx") == 0);
    CHECK(parse_huge_page_filename("hugepages-1024") == 0);
    CHECK(parse_huge_page_filename("hugepages-kB") == 0);
    CHECK(parse_huge_page_filename("hugepages-1024kB") == 1048576);
    CHECK(parse_huge_page_filename("hugepages-1024MB") == 0);
    CHECK(parse_huge_page_filename("hugepage-1024kB") == 0);
}

} // namespace internal

#ifdef __linux__

TEST_CASE("default_huge_page_size") {
    std::size_t const result = default_huge_page_size();
    if (result > 0) {
        bool const is_power_of_2 = (result & (result - 1)) == 0;
        CHECK(is_power_of_2);
    }
}

TEST_CASE("huge_page_sizes") {
    auto const result = huge_page_sizes();
    std::size_t prev = 0;
    for (auto s : result) {
        CHECK(s > prev); // Unique, sorted, and > 0.
        prev = s;
        bool const is_power_of_2 = (s & (s - 1)) == 0;
        CHECK(is_power_of_2);
    }
}

#endif

} // namespace partake::daemon
