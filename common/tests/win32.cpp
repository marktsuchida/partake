/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "win32.hpp"

#ifdef _WIN32

#include "testing.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace partake::common::win32 {

TEST_CASE("win32::strerror") {
    CHECK_FALSE(strerror(ERROR_ACCESS_DENIED).empty());
    CHECK_FALSE(strerror(0).empty());
    CHECK_FALSE(strerror(unsigned(-1)).empty());
}

TEST_CASE("win32_handle") {
    CHECK(win32_handle::invalid_handle() == INVALID_HANDLE_VALUE);

    win32_handle default_instance;
    CHECK_FALSE(default_instance.is_valid());
    CHECK(default_instance.get() == INVALID_HANDLE_VALUE);
    REQUIRE(default_instance.close());
    CHECK(default_instance.close()); // Idempotent

    testing::tempdir const td;
    auto path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));

    HANDLE h_file = CreateFileA(
        path.string().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
    REQUIRE(h_file != INVALID_HANDLE_VALUE);

    win32_handle h(h_file);
    CHECK(h.is_valid());
    CHECK(h.get() == h_file);

    // NOLINTBEGIN(bugprone-use-after-move)
    win32_handle other(std::move(h));
    CHECK_FALSE(h.is_valid());
    CHECK(other.is_valid());
    CHECK(other.get() == h_file);

    h = std::move(other);
    CHECK_FALSE(other.is_valid());
    CHECK(h.is_valid());
    CHECK(h.get() == h_file);
    // NOLINTEND(bugprone-use-after-move)

    REQUIRE(h.close());
    CHECK(h.close()); // Idempotent
}

TEST_CASE("win32::unlinkable") {
    unlinkable default_instance;
    CHECK_FALSE(default_instance.is_valid());
    CHECK(default_instance.name().empty());
    REQUIRE(default_instance.unlink());
    CHECK(default_instance.unlink()); // Idempotent

    testing::tempdir const td;
    auto f = testing::unique_file_with_data(
        td.path(), testing::make_test_filename(__FILE__, __LINE__), {});

    unlinkable unlk(f.path().string());
    CHECK(unlk.is_valid());
    CHECK(unlk.name() == f.path().string());

    // NOLINTBEGIN(bugprone-use-after-move)
    unlinkable other(std::move(unlk));
    CHECK_FALSE(unlk.is_valid());
    CHECK(other.is_valid());
    CHECK(other.name() == f.path().string());

    unlk = std::move(other);
    CHECK_FALSE(other.is_valid());
    CHECK(unlk.is_valid());
    CHECK(unlk.name() == f.path().string());
    // NOLINTEND(bugprone-use-after-move)

    REQUIRE(unlk.unlink());
    CHECK_FALSE(std::filesystem::exists(f.path()));
    CHECK(unlk.unlink());
}

} // namespace partake::common::win32

#endif // _WIN32
