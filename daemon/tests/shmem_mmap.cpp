/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_mmap.hpp"

#ifndef _WIN32

#include "page_size.hpp"
#include "random.hpp"
#include "testing.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cerrno>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/limits.h>
#endif

namespace partake::daemon {

namespace internal {

TEST_CASE("create_posix_shmem") {
    GIVEN("unique shmem name") {
        auto const name = "/partake-test-" + common::random_string(10);

        // Structured bindings cannot be captured by lambdas (C++17), so use
        // std::tie() instead.
        common::posix::unlinkable unlk;
        common::posix::file_descriptor fd;

        SECTION("create, no-force") {
            std::tie(unlk, fd) = create_posix_shmem(name, false);
            CHECK(unlk.is_valid());
            CHECK(fd.is_valid());
            CHECK(unlk.name() == name);

            common::posix::unlinkable unlk2;
            common::posix::file_descriptor fd2;

            SECTION("create with existing name, no-force") {
                std::tie(unlk2, fd2) = create_posix_shmem(name, false);
                CHECK_FALSE(unlk2.is_valid());
                CHECK_FALSE(fd2.is_valid());
                CHECK(unlk2.name().empty());
            }

            SECTION("create with existing name, force") {
                std::tie(unlk2, fd2) = create_posix_shmem(name, true);
                CHECK(unlk2.is_valid());
                CHECK(fd2.is_valid());
                CHECK(unlk2.name() == name);

                SECTION("force-created shmem supports ftruncate()") {
                    CHECK(::ftruncate(fd2.get(), 16384) == 0);
                    CAPTURE(errno);
                }
            }
        }

        SECTION("create, force") {
            std::tie(unlk, fd) = create_posix_shmem(name, true);
            CHECK(unlk.is_valid());
            CHECK(fd.is_valid());
            CHECK(unlk.name() == name);

            SECTION("let destructor clean up") {}

            SECTION("explicitly unlink and close") {
                unlk.unlink();
                fd.close();
            }
        }
    }
}

TEST_CASE("create_regular_file") {
    GIVEN("unique file name") {
        testing::tempdir const td;
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));

        common::posix::unlinkable unlk;
        common::posix::file_descriptor fd;

        SECTION("create, no-force") {
            std::tie(unlk, fd) = create_regular_file(path.string(), false);
            CHECK(unlk.is_valid());
            CHECK(fd.is_valid());
            CHECK(unlk.name() == path.string());

            common::posix::unlinkable unlk2;
            common::posix::file_descriptor fd2;

            SECTION("create with existing name, no-force") {
                std::tie(unlk2, fd2) =
                    create_regular_file(path.string(), false);
                CHECK_FALSE(unlk2.is_valid());
                CHECK_FALSE(fd2.is_valid());
                CHECK(unlk2.name().empty());
            }

            SECTION("create with existing name, force") {
                std::tie(unlk2, fd2) =
                    create_regular_file(path.string(), true);
                CHECK(unlk2.is_valid());
                CHECK(fd2.is_valid());
                CHECK(unlk2.name() == path.string());
            }
        }

        SECTION("create, force") {
            std::tie(unlk, fd) = create_regular_file(path.string(), true);
            CHECK(unlk.is_valid());
            CHECK(fd.is_valid());
            CHECK(unlk.name() == path.string());

            SECTION("let destructor clean up") {}

            SECTION("explicitly unlink and close") {
                unlk.unlink();
                fd.close();
            }
        }
    }
}

TEST_CASE("mmap_mapping") {
    SECTION("default instance") {
        mmap_mapping mm;
        CHECK_FALSE(mm.is_valid());
        CHECK(mm.size() == 0);
        CHECK(mm.address() == nullptr);

        SECTION("let destructor clean up") {}

        SECTION("explicitly unmap") {
            REQUIRE(mm.unmap());
            CHECK(mm.unmap()); // Idempotent
        }
    }

    GIVEN("a regular-file fd") {
        testing::tempdir const td;
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));
        common::posix::unlinkable unlk;
        common::posix::file_descriptor fd;
        std::tie(unlk, fd) = create_regular_file(path.string(), true);
        REQUIRE(unlk.is_valid());
        REQUIRE(fd.is_valid());

        SECTION("create mapping") {
            auto mm = mmap_mapping(16384, fd);
            CHECK(mm.is_valid());
            CHECK(mm.size() == 16384);
            CHECK(mm.address() != nullptr);
            void *addr = mm.address();

            // NOLINTBEGIN(bugprone-use-after-move)
            SECTION("move-construct") {
                mmap_mapping const other(std::move(mm));
                CHECK_FALSE(mm.is_valid());
                CHECK(other.is_valid());
                CHECK(other.size() == 16384);
                CHECK(other.address() == addr);
            }

            SECTION("move-assign") {
                mmap_mapping other;
                other = std::move(mm);
                CHECK_FALSE(mm.is_valid());
                CHECK(other.is_valid());
                CHECK(other.size() == 16384);
                CHECK(other.address() == addr);
            }
            // NOLINTEND(bugprone-use-after-move)

            SECTION("let destructor clean up") {}

            SECTION("explicitly unmap") {
                REQUIRE(mm.unmap());
                CHECK(mm.unmap()); // Idempotent
            }
        }
    }

    GIVEN("a write-only regular-file fd") {
        testing::tempdir const td;
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        auto fd = common::posix::file_descriptor(::open(
            path.string().c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR));
        REQUIRE(fd.is_valid());
        auto unlk = common::posix::unlinkable(path.string());

        SECTION("mmap failure leaves mapping empty") {
            auto mm = mmap_mapping(16384, fd);
            CHECK_FALSE(mm.is_valid());
            CHECK(mm.size() == 0);
            CHECK(mm.address() == nullptr);
            CHECK(mm.unmap()); // No-op on empty mapping.
        }
    }
}

TEST_CASE("generate_posix_shmem_name") {
    auto n = generate_posix_shmem_name();
    CHECK_FALSE(n.empty());
    CHECK(n.size() <= 31); // macOS compatibility
    CHECK(n.front() == '/');
}

TEST_CASE("generate_filename") {
    auto n = generate_filename();
    CHECK_FALSE(n.empty());
    CHECK(n.size() < PATH_MAX);
    CHECK(n.front() == '/'); // Absolute path
}

} // namespace internal

TEST_CASE("create_posix_mmap_shmem") {
    // NOLINTNEXTLINE(readability-magic-numbers)
    auto shm = create_posix_mmap_shmem(100);
    CHECK(shm.is_valid());
    CHECK_FALSE(shm.name().empty());
    CHECK(shm.address() != nullptr);
    CHECK(shm.size() == page_size());
    CHECK(shm.unlink());
    CHECK(shm.unmap());
}

TEST_CASE("create_posix_file_shmem") {
    // NOLINTNEXTLINE(readability-magic-numbers)
    auto shm = create_file_mmap_shmem(100);
    CHECK(shm.is_valid());
    CHECK_FALSE(shm.name().empty());
    CHECK(shm.address() != nullptr);
    CHECK(shm.size() == page_size());
    CHECK(shm.unlink());
    CHECK(shm.unmap());
}

} // namespace partake::daemon

#endif // _WIN32
