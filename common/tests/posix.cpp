/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "posix.hpp"

#ifndef _WIN32

#include "testing.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <filesystem>
#include <utility>

#include <fcntl.h> // open

namespace partake::common::posix {

TEST_CASE("posix::strerror") {
    CHECK_FALSE(strerror(EACCES).empty());
    CHECK_FALSE(strerror(0).empty());
    CHECK_FALSE(strerror(-1).empty());
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
