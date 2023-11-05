/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "posix.hpp"

#ifndef _WIN32

#include "random.hpp"
#include "testing.hpp"

#include <doctest.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <cstdlib> // mkstemp
#include <cstring> // strerror_r
#include <filesystem>

#include <unistd.h>

namespace partake::common::posix {

auto strerror(int errn) -> std::string {
    // See Linux man strerror_r(3) regarding POSIX vs GNU variants. We assign
    // the result to a specific type so as to catch any misconfiguration.
    std::string ret;
    ret.resize(512);
#if _GNU_SOURCE || (__linux__ && (_POSIX_C_SOURCE < 200112L))
    char *msg = ::strerror_r(errn, ret.data(), ret.size());
    return msg;
#else
    int const failed = ::strerror_r(errn, ret.data(), ret.size());
    if (failed != 0)
        return fmt::format("Unknown error {}", errn);
    ret.resize(std::strlen(ret.data()));
    return ret;
#endif
}

TEST_CASE("posix::strerror") {
    CHECK_FALSE(strerror(EACCES).empty());
    CHECK_FALSE(strerror(0).empty());
    CHECK_FALSE(strerror(-1).empty());
}

auto file_descriptor::close() -> bool {
    if (fd == invalid_fd)
        return true;
    bool ret = false;
    errno = 0;
    if (::close(fd) != 0) {
        auto err = errno;
        auto msg = strerror(err);
        lgr->error("close: fd {}: {} ({})", fd, msg, err);
    } else {
        lgr->info("close: fd {}: success", fd);
        ret = true;
    }
    fd = invalid_fd;
    return ret;
}

TEST_CASE("file_descriptor") {
    file_descriptor default_instance;
    CHECK_FALSE(default_instance.is_valid());
    CHECK(default_instance.get() == file_descriptor::invalid_fd);
    REQUIRE(default_instance.close());
    CHECK(default_instance.close()); // Idempotent

    testing::tempdir const td;
    auto path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    int const fd = ::open(path.string().data(), O_CREAT | O_EXCL, 0600);
    REQUIRE(fd >= 0);
    testing::auto_delete_file const adf(path);

    file_descriptor fdo(fd);
    CHECK(fdo.is_valid());
    CHECK(fdo.get() == fd);

    // NOLINTBEGIN(bugprone-use-after-move)
    file_descriptor other(std::move(fdo));
    CHECK_FALSE(fdo.is_valid());
    CHECK(other.is_valid());
    CHECK(other.get() == fd);

    fdo = std::move(other);
    CHECK_FALSE(other.is_valid());
    CHECK(fdo.is_valid());
    CHECK(fdo.get() == fd);
    // NOLINTEND(bugprone-use-after-move)

    REQUIRE(fdo.close());
    CHECK(fdo.close()); // Idempotent
}

unlinkable::unlinkable(std::string_view name,
                       std::shared_ptr<spdlog::logger> logger)
    : unlinkable(name, ::unlink, "unlink", std::move(logger)) {}

TEST_CASE("posix::unlinkable") {
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
    CHECK_FALSE(std::filesystem::exists(f.path().string()));
    CHECK(unlk.unlink()); // Idempotent
}

} // namespace partake::common::posix

#endif // _WIN32
