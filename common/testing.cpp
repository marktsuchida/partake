/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "testing.hpp"

#include <array>

namespace partake::testing {

TEST_CASE("tempdir") {
    std::filesystem::path p;
    {
        tempdir const td;
        p = td.path();
        CHECK_FALSE(p.empty());
        CHECK(std::filesystem::is_directory(p));
    }
    CHECK_FALSE(std::filesystem::is_directory(p));
}

TEST_CASE("unique_path") {
    auto p = unique_path("blah", "bleh");
    CHECK(p.parent_path() == "blah");
    CHECK(p.filename().string().substr(0, 5) == "bleh-");
}

TEST_CASE("auto_delete_file") {
    tempdir const td;
    auto p = unique_path(td.path(), make_test_filename(__FILE__, __LINE__));
    CHECK_FALSE(std::filesystem::exists(p));
    {
        std::ofstream s(p, std::ios::binary);
        s.write("abc", 4);
    }
    CHECK(std::filesystem::exists(p));
    {
        auto_delete_file const adf(p);
        CHECK(std::filesystem::exists(p));
    }
    CHECK_FALSE(std::filesystem::exists(p));
}

TEST_CASE("unique_file_with_data") {
    std::vector<std::uint8_t> data{'a', 'b', 'c'};
    tempdir const td;
    auto f = unique_file_with_data(
        td.path(), make_test_filename(__FILE__, __LINE__), data);
    {
        std::ifstream s(f.path(), std::ios::binary);
        std::array<char, 5> buf{};
        s.read(buf.data(), buf.size());
        CHECK(s.eof());
        CHECK(s.gcount() == 3);
        CHECK(buf[0] == 'a');
        CHECK(buf[1] == 'b');
        CHECK(buf[2] == 'c');
    }
}

} // namespace partake::testing
