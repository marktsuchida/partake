/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "cli.hpp"

#include <catch2/catch_test_macros.hpp>

#include <CLI/CLI.hpp>

#include <cstddef>
#include <string>

namespace partake::daemon::internal {

TEST_CASE("parse_size_suffix") {
    CHECK(parse_size_suffix("0") == "0");
    CHECK(parse_size_suffix("1") == "1");
    CHECK(parse_size_suffix("12345") == "12345");

    CHECK(parse_size_suffix("0B") == "0");
    CHECK(parse_size_suffix("1B") == "1");
    CHECK(parse_size_suffix("12345B") == "12345");

    CHECK(parse_size_suffix("0k") == "0");
    CHECK(parse_size_suffix("1k") == "1024");
    CHECK(parse_size_suffix("12345k") == "12641280");

    CHECK(parse_size_suffix("0M") == "0");
    CHECK(parse_size_suffix("1M") == "1048576");

    CHECK(parse_size_suffix("0G") == "0");
    CHECK(parse_size_suffix("1G") == "1073741824");

    static constexpr auto max = [] {
        if constexpr (sizeof(std::size_t) == 4) {
            return "2147483647"; // 2^31 - 1
        } else {
            static_assert(sizeof(std::size_t) == 8);
            return "9223372036854775807"; // 2^63 - 1
        }
    }();
    CHECK(parse_size_suffix(max) == max);

    CHECK_THROWS_AS(parse_size_suffix(""), CLI::ValidationError);
    CHECK_THROWS_AS(parse_size_suffix("b"), CLI::ValidationError);
    CHECK_THROWS_AS(parse_size_suffix("-1"), CLI::ValidationError);
    CHECK_THROWS_AS(parse_size_suffix("1n"), CLI::ValidationError);
    CHECK_THROWS_AS(parse_size_suffix("1 B"), CLI::ValidationError);

    static constexpr auto max_plus_one = [] {
        if constexpr (sizeof(std::size_t) == 4) {
            return "2147483648"; // 2^31
        } else {
            static_assert(sizeof(std::size_t) == 8);
            return "9223372036854775808"; // 2^63
        }
    }();
    CHECK_THROWS_AS(parse_size_suffix(max_plus_one), CLI::ValidationError);
}

TEST_CASE("validate_segment_type") {
    cli_args args;
    CHECK(validate_segment_type<false>(args).value() == shmem_type::posix);
    CHECK(validate_segment_type<true>(args).value() == shmem_type::win32);

    args = cli_args();
    args.filename = "myfile";
    CHECK(validate_segment_type<false>(args).value() ==
          shmem_type::posix_file);
    CHECK(validate_segment_type<true>(args).value() == shmem_type::win32_file);

    args = cli_args();
    args.posix = true;
    CHECK(validate_segment_type(args).value() == shmem_type::posix);
    args = cli_args();
    args.systemv = true;
    CHECK(validate_segment_type(args).value() == shmem_type::system_v);
    args = cli_args();
    args.windows = true;
    CHECK(validate_segment_type(args).value() == shmem_type::win32);

    args = cli_args();
    args.posix = true;
    args.systemv = true;
    CHECK_FALSE(validate_segment_type(args).has_value());
    args = cli_args();
    args.posix = true;
    args.windows = true;
    CHECK_FALSE(validate_segment_type(args).has_value());
    args = cli_args();
    args.posix = true;
    args.filename = "x";
    CHECK_FALSE(validate_segment_type(args).has_value());
    args = cli_args();
    args.windows = true;
    args.filename = "x";
    CHECK_FALSE(validate_segment_type(args).has_value());
}

TEST_CASE("validate_posix_shmem_name") {
    CHECK(validate_posix_shmem_name("").value().empty());
    CHECK_FALSE(validate_posix_shmem_name("/").has_value());
    CHECK(validate_posix_shmem_name("/a").value() == "/a");
    CHECK_FALSE(validate_posix_shmem_name("/a/").has_value());
}

TEST_CASE("validate_sysv_shmem_name") {
    CHECK(validate_sysv_shmem_name("").value() == 0);
    CHECK(validate_sysv_shmem_name("0").value() == 0);
    CHECK(validate_sysv_shmem_name("1").value() == 1);
    CHECK(validate_sysv_shmem_name("-1").value() == -1);
    CHECK_FALSE(validate_sysv_shmem_name("2147483648").has_value());
    CHECK_FALSE(validate_sysv_shmem_name("abc").has_value());
}

TEST_CASE("validate_socket_path") {
    CHECK(validate_socket_path("/tmp/some.sock").value().path() ==
          "/tmp/some.sock");
    CHECK_FALSE(validate_socket_path("").has_value());
    auto const too_long = validate_socket_path(std::string(200, 'a'));
    CHECK_FALSE(too_long.has_value());
    CHECK_FALSE(too_long.error().empty());
}

TEST_CASE("validate_win32_shmem_name") {
    CHECK(validate_win32_shmem_name("").value().empty());
    CHECK_FALSE(validate_win32_shmem_name("x").has_value());
    CHECK_FALSE(validate_win32_shmem_name(R"(Local\)").has_value());
    CHECK(validate_win32_shmem_name(R"(Local\x)").value() == R"(Local\x)");
    CHECK_FALSE(validate_win32_shmem_name(R"(Local\x\)").has_value());
}

} // namespace partake::daemon::internal
