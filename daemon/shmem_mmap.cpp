/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "shmem_mmap.hpp"

#ifndef _WIN32

#include "random.hpp"
#include "testing.hpp"

#include <doctest.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/posix_shm.h>
#endif

#ifdef __linux__
#include <linux/limits.h>
#endif

namespace partake::daemon {

namespace internal {

auto create_posix_shmem(std::string const &name, bool force) noexcept
    -> std::pair<posix::unlinkable, posix::file_descriptor> {
#ifdef __APPLE__
    if (force) {
        // On macOS, ftruncate() only succeeds once on a POSIX shmem, so we
        // need to unlink before reusing a name (even with shm_open(O_CREAT)
        // below).
        errno = 0;
        if (::shm_unlink(name.c_str())) {
            auto err = errno;
            if (err != ENOENT) {
                auto msg = posix::strerror(err);
                spdlog::error("shm_unlink: {}: {} ({})", name, msg, err);
            }
        }
    }
#endif

    errno = 0;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
    auto fd = posix::file_descriptor(::shm_open(
        name.c_str(), O_RDWR | O_CREAT | (force ? 0 : O_EXCL), 0666));
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    if (not fd.is_valid()) {
        int err = errno;
        auto msg = posix::strerror(err);
        spdlog::error("shm_open: {}: {} ({})", name, msg, err);
        return {};
    }
    spdlog::info("shm_open: {}: success; fd {}", name, fd.get());
    return {posix::unlinkable(name, ::shm_unlink, "shm_unlink"),
            std::move(fd)};
}

TEST_CASE("create_posix_shmem") {
    GIVEN("unique shmem name") {
        auto const name = "/partake-test-" + random_string(10);

        // Structured binding doesn't work with lambda capture (used by
        // doctest), so use std::tie() instead.
        posix::unlinkable unlk;
        posix::file_descriptor fd;

        SUBCASE("create, no-force") {
            std::tie(unlk, fd) = create_posix_shmem(name, false);
            CHECK(unlk.is_valid());
            CHECK(fd.is_valid());
            CHECK(unlk.name() == name);

            posix::unlinkable unlk2;
            posix::file_descriptor fd2;

            SUBCASE("create with existing name, no-force") {
                std::tie(unlk2, fd2) = create_posix_shmem(name, false);
                CHECK_FALSE(unlk2.is_valid());
                CHECK_FALSE(fd2.is_valid());
                CHECK(unlk2.name().empty());
            }

            SUBCASE("create with existing name, force") {
                std::tie(unlk2, fd2) = create_posix_shmem(name, true);
                CHECK(unlk2.is_valid());
                CHECK(fd2.is_valid());
                CHECK(unlk2.name() == name);

                SUBCASE("force-created shmem supports ftruncate()") {
                    CHECK(::ftruncate(fd2.get(), 16384) == 0);
                    CAPTURE(errno);
                }
            }
        }

        SUBCASE("create, force") {
            std::tie(unlk, fd) = create_posix_shmem(name, true);
            CHECK(unlk.is_valid());
            CHECK(fd.is_valid());
            CHECK(unlk.name() == name);

            SUBCASE("let destructor clean up") {}

            SUBCASE("explicitly unlink and close") {
                unlk.unlink();
                fd.close();
            }
        }
    }
}

auto create_regular_file(std::string const &path, bool force) noexcept
    -> std::pair<posix::unlinkable, posix::file_descriptor> {
    errno = 0;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
    auto fd = posix::file_descriptor(
        ::open(path.c_str(),
               O_RDWR | O_CREAT | (force ? 0 : O_EXCL) | O_CLOEXEC, 0666));
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    if (not fd.is_valid()) {
        int err = errno;
        auto msg = posix::strerror(err);
        spdlog::error("open: {}: {} ({})", path, msg, err);
        return {};
    }
    spdlog::info("open: {}: success; fd {}", path, fd.get());
    return {posix::unlinkable(path), std::move(fd)};
}

TEST_CASE("create_regular_file") {
    GIVEN("unique file name") {
        testing::tempdir const td;
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));

        posix::unlinkable unlk;
        posix::file_descriptor fd;

        SUBCASE("create, no-force") {
            std::tie(unlk, fd) = create_regular_file(path.string(), false);
            CHECK(unlk.is_valid());
            CHECK(fd.is_valid());
            CHECK(unlk.name() == path.string());

            posix::unlinkable unlk2;
            posix::file_descriptor fd2;

            SUBCASE("create with existing name, no-force") {
                std::tie(unlk2, fd2) =
                    create_regular_file(path.string(), false);
                CHECK_FALSE(unlk2.is_valid());
                CHECK_FALSE(fd2.is_valid());
                CHECK(unlk2.name().empty());
            }

            SUBCASE("create with existing name, force") {
                std::tie(unlk2, fd2) =
                    create_regular_file(path.string(), true);
                CHECK(unlk2.is_valid());
                CHECK(fd2.is_valid());
                CHECK(unlk2.name() == path.string());
            }
        }

        SUBCASE("create, force") {
            std::tie(unlk, fd) = create_regular_file(path.string(), true);
            CHECK(unlk.is_valid());
            CHECK(fd.is_valid());
            CHECK(unlk.name() == path.string());

            SUBCASE("let destructor clean up") {}

            SUBCASE("explicitly unlink and close") {
                unlk.unlink();
                fd.close();
            }
        }
    }
}

mmap_mapping::mmap_mapping(std::size_t size,
                           posix::file_descriptor const &fd) noexcept
    : siz(size) {
    if (not fd.is_valid())
        return;

    errno = 0;
    if (::ftruncate(fd.get(), static_cast<off_t>(size))) {
        int err = errno;
        auto msg = posix::strerror(err);
        spdlog::error("ftruncate: fd {}, size {}: {} ({})", fd.get(), size,
                      msg, err);
        return;
    }
    spdlog::info("ftruncate: fd {}, size {}: success", fd.get(), size);

    if (size > 0) {
        errno = 0;
        addr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd.get(), 0);
        if (addr == nullptr) {
            int err = errno;
            auto msg = posix::strerror(err);
            spdlog::error("mmap: fd {}, size {}: {} ({})", fd.get(), size, msg,
                          err);
        } else {
            spdlog::info("mmap: fd {}, size {}: success; addr {}", fd.get(),
                         size, addr);
        }
    }
}

auto mmap_mapping::unmap() noexcept -> bool {
    if (addr == nullptr)
        return true;
    bool ret = false;
    errno = 0;
    if (::munmap(addr, siz)) {
        int err = errno;
        auto msg = posix::strerror(err);
        spdlog::error("munmap: addr {}: {} ({})", addr, msg, err);
    } else {
        spdlog::info("munmap: addr {}: success", addr);
        ret = true;
    }
    siz = 0;
    addr = nullptr;
    return ret;
}

TEST_CASE("mmap_mapping") {
    SUBCASE("default instance") {
        mmap_mapping mm;
        CHECK_FALSE(mm.is_valid());
        CHECK(mm.size() == 0);
        CHECK(mm.address() == nullptr);

        SUBCASE("let destructor clean up") {}

        SUBCASE("explicitly unmap") {
            REQUIRE(mm.unmap());
            CHECK(mm.unmap()); // Idempotent
        }
    }

    GIVEN("a regular-file fd") {
        testing::tempdir const td;
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));
        posix::unlinkable unlk;
        posix::file_descriptor fd;
        std::tie(unlk, fd) = create_regular_file(path.string(), true);
        REQUIRE(unlk.is_valid());
        REQUIRE(fd.is_valid());

        SUBCASE("create mapping") {
            auto mm = mmap_mapping(16384, fd);
            CHECK(mm.is_valid());
            CHECK(mm.size() == 16384);
            CHECK(mm.address() != nullptr);
            void *addr = mm.address();

            // NOLINTBEGIN(bugprone-use-after-move)
            SUBCASE("move-construct") {
                mmap_mapping const other(std::move(mm));
                CHECK_FALSE(mm.is_valid());
                CHECK(other.is_valid());
                CHECK(other.size() == 16384);
                CHECK(other.address() == addr);
            }

            SUBCASE("move-assign") {
                mmap_mapping other;
                other = std::move(mm);
                CHECK_FALSE(mm.is_valid());
                CHECK(other.is_valid());
                CHECK(other.size() == 16384);
                CHECK(other.address() == addr);
            }
            // NOLINTEND(bugprone-use-after-move)

            SUBCASE("let destructor clean up") {}

            SUBCASE("explicitly unmap") {
                REQUIRE(mm.unmap());
                CHECK(mm.unmap()); // Idempotent
            }
        }
    }
}

auto generate_posix_shmem_name() noexcept -> std::string {
    // Max: macOS 31, Linux 255, FreeBSD 1023.
    static constexpr std::size_t name_len = 31;
    std::string name = "/partake-";
    name += random_string(name_len - name.size()); // 22 random chars
    return name;
}

TEST_CASE("generate_posix_shmem_name") {
    auto n = generate_posix_shmem_name();
    CHECK_FALSE(n.empty());
    CHECK(n.size() <= 31); // macOS compatibility
    CHECK(n.front() == '/');
}

auto generate_filename() noexcept -> std::string {
    auto const filename = "partake-" + random_string(24);
#ifdef __APPLE__
    // Avoid the long, messy path returned by temp_directory_path().
    // Assume no macOS system is without /tmp.
    auto p = std::filesystem::path("/tmp");
#else
    auto p = std::filesystem::temp_directory_path();
#endif
    return p / filename;
}

TEST_CASE("generate_filename") {
    auto n = generate_filename();
    CHECK_FALSE(n.empty());
    CHECK(n.size() < PATH_MAX);
    CHECK(n.front() == '/'); // Absolute path
}

} // namespace internal

auto create_posix_mmap_shmem(std::string const &name, std::size_t size,
                             bool force) noexcept -> mmap_shmem {
    auto [shmem, fd] = internal::create_posix_shmem(name, force);
    return mmap_shmem(std::move(shmem), fd, size);
}

auto create_posix_mmap_shmem(std::size_t size) noexcept -> mmap_shmem {
    auto name = internal::generate_posix_shmem_name();
    return create_posix_mmap_shmem(name, size, false);
}

TEST_CASE("create_posix_mmap_shmem") {
    auto shm = create_posix_mmap_shmem(4096);
    CHECK(shm.is_valid());
    CHECK_FALSE(shm.name().empty());
    CHECK(shm.address() != nullptr);
    CHECK(shm.unlink());
    CHECK(shm.unmap());
}

auto create_file_mmap_shmem(std::string const &name, std::size_t size,
                            bool force) noexcept -> mmap_shmem {
    auto [file, fd] = internal::create_regular_file(name, force);
    return mmap_shmem(std::move(file), fd, size);
}

auto create_file_mmap_shmem(std::size_t size) noexcept -> mmap_shmem {
    auto name = internal::generate_filename();
    return create_file_mmap_shmem(name, size, false);
}

TEST_CASE("create_posix_file_shmem") {
    auto shm = create_file_mmap_shmem(4096);
    CHECK(shm.is_valid());
    CHECK_FALSE(shm.name().empty());
    CHECK(shm.address() != nullptr);
    CHECK(shm.unlink());
    CHECK(shm.unmap());
}

} // namespace partake::daemon

#endif // _WIN32
