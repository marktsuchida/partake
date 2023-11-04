/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "win32.hpp"

#ifdef _WIN32

#include "testing.hpp"

#include <doctest.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace partake::common::win32 {

auto strerror(unsigned err) -> std::string {
    std::string ret;
    char *msg = nullptr;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPSTR>(&msg), 0, nullptr) != 0) {
        ret = msg;
        LocalFree(reinterpret_cast<HLOCAL>(msg));
    } else {
        ret = fmt::format("Unknown error {}", err);
    }
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    return ret;
}

TEST_CASE("win32::strerror") {
    CHECK_FALSE(strerror(ERROR_ACCESS_DENIED).empty());
    CHECK_FALSE(strerror(0).empty());
    CHECK_FALSE(strerror(unsigned(-1)).empty());
}

auto win32_handle::close() -> bool {
    if (h == invalid_handle())
        return true;
    bool ret = false;
    if (CloseHandle(h) == 0) {
        auto err = GetLastError();
        auto msg = strerror(err);
        lgr->error("CloseHandle: {}: {} ({})", h, msg, err);
    } else {
        lgr->info("CloseHandle: {}: success", h);
        ret = true;
    }
    h = invalid_handle();
    return ret;
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

unlinkable::unlinkable(std::string_view name,
                       std::shared_ptr<spdlog::logger> logger)
    : unlinkable(name, DeleteFileA, "DeleteFile", std::move(logger)) {}

auto unlinkable::unlink() -> bool {
    if (nm.empty())
        return true;
    bool ret = false;
    if (unlink_fn(nm.c_str()) == 0) {
        auto err = GetLastError();
        auto msg = strerror(err);
        lgr->error("{}: {}: {} ({})", fn_name, nm, msg, err);
    } else {
        lgr->info("{}: {}: success", fn_name, nm);
        ret = true;
    }
    nm.clear();
    return ret;
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
