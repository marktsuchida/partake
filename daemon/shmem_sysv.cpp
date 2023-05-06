/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "shmem_sysv.hpp"

#ifndef _WIN32

#include "posix.hpp"
#include "random.hpp"

#include <doctest.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <type_traits>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>

namespace partake::daemon {

// We use 'int' in our interface to avoid including Unix headers in our header.
static_assert(std::is_same_v<int, key_t>);

// We assume IPC_PRIVATE equals 0 and use 0 as request to auto-select the key.
static_assert(IPC_PRIVATE == 0);

namespace internal {

auto create_sysv_shmem_id(int key, std::size_t size, bool force = false,
                          bool use_huge_pages = false) noexcept
    -> sysv_shmem_id {
    if (size == 0)
        return {};

    if (force && key != IPC_PRIVATE) {
        // We cannot reuse an existing key if the size grows (EEXIST; EINVAL on
        // macOS), so remove it first.
        errno = 0;
        int id = ::shmget(key, 0, 0);
        if (id < 0) {
            auto e = errno;
            if (e != ENOENT) {
                auto msg = posix::strerror(e);
                spdlog::error("shmget: key {}: {} ({})", key, msg, e);
            }
        } else {
            errno = 0;
            if (::shmctl(id, IPC_RMID, nullptr)) {
                auto e = errno;
                if (e != ENOENT) {
                    auto msg = posix::strerror(e);
                    spdlog::error("shmctl IPC_RMID: id {}: {} ({})", id, msg,
                                  e);
                }
            }
        }
    }

#ifdef __linux__
    int const huge_pages_flag = use_huge_pages ? SHM_HUGETLB : 0;
#else
    if (use_huge_pages) {
        spdlog::error("shmget: huge pages not supported on this platform");
        return {};
    }
    int const huge_pages_flag = 0;
#endif
    errno = 0;
    int id =
        ::shmget(key, size,
                 IPC_CREAT | (force ? 0 : IPC_EXCL) | huge_pages_flag | 0666);
    if (id < 0) {
        auto err = errno;
        auto msg = posix::strerror(err);
        spdlog::error("shmget: key {}: {} ({})", key, msg, err);
        return {};
    }

    spdlog::info("shmget: key {}: success; id {}", key, id);
    return sysv_shmem_id(id);
}

auto sysv_shmem_id::remove() noexcept -> bool {
    if (shmid < 0)
        return true;
    bool ret = false;
    errno = 0;
    if (::shmctl(shmid, IPC_RMID, nullptr)) {
        int e = errno;
        auto msg = posix::strerror(e);
        spdlog::error("shmctl IPC_RMID: id {}: {} ({})", shmid, msg, e);
    } else {
        spdlog::info("shmctl IPC_RMID: id {}: success", shmid);
        ret = true;
    }
    shmid = -1;
    return ret;
}

TEST_CASE("sysv_shmem_id") {
    SUBCASE("default instance") {
        sysv_shmem_id shmid;
        CHECK_FALSE(shmid.is_valid());
        CHECK(shmid.id() == -1);

        SUBCASE("let destructor clean up") {}

        SUBCASE("explicitly remove") {
            REQUIRE(shmid.remove());
            CHECK(shmid.remove()); // Idempotent
        }
    }

    SUBCASE("create id by finding non-existent key") {
        key_t key = 0;
        sysv_shmem_id shmid;
        while (not shmid.is_valid()) {
            ++key;
            shmid = create_sysv_shmem_id(key, 8192);
        }
        CHECK(shmid.id() >= 0);

        SUBCASE("create with existing key, no-force") {
            SUBCASE("same size") {
                auto shmid2 = create_sysv_shmem_id(key, 16384, false);
                CHECK_FALSE(shmid2.is_valid());
                CHECK(shmid2.id() == -1);
            }

            SUBCASE("larger size") {
                auto shmid2 = create_sysv_shmem_id(key, 32768, false);
                CHECK_FALSE(shmid2.is_valid());
                CHECK(shmid2.id() == -1);
            }
        }

        SUBCASE("create with existing key, force") {
            SUBCASE("same size") {
                auto shmid2 = create_sysv_shmem_id(key, 16384, true);
                CHECK(shmid2.is_valid());
                CHECK(shmid2.id() >= 0);
            }

            SUBCASE("larger size") {
                auto shmid2 = create_sysv_shmem_id(key, 32768, true);
                CHECK(shmid2.is_valid());
                CHECK(shmid2.id() >= 0);
            }
        }

        // NOLINTBEGIN(bugprone-use-after-move)
        SUBCASE("move-construct") {
            auto id = shmid.id();
            sysv_shmem_id const other(std::move(shmid));
            CHECK_FALSE(shmid.is_valid());
            CHECK(other.is_valid());
            CHECK(other.id() == id);
        }

        SUBCASE("move-assign") {
            auto id = shmid.id();
            sysv_shmem_id other;
            other = std::move(shmid);
            CHECK_FALSE(shmid.is_valid());
            CHECK(other.is_valid());
            CHECK(other.id() == id);
        }
        // NOLINTEND(bugprone-use-after-move)
    }

    SUBCASE("create id with IPC_PRIVATE") {
        auto shmid = create_sysv_shmem_id(IPC_PRIVATE, 16384, false);
        CHECK(shmid.is_valid());
        CHECK(shmid.id() >= 0);

        SUBCASE("let destructor clean up") {}

        SUBCASE("explicitly remove") {
            REQUIRE(shmid.remove());
            CHECK(shmid.remove()); // Idempotent
        }
    }
}

sysv_shmem_attachment::sysv_shmem_attachment(int id) noexcept {
    if (id < 0)
        return;
    errno = 0;
    void *a = ::shmat(id, nullptr, 0);
    if (a == (void *)-1) { // NOLINT
        auto err = errno;
        auto msg = posix::strerror(err);
        spdlog::error("shmat: id {}: {} ({})", id, msg, err);
    } else {
        spdlog::info("shmat: id {}: success; addr {}", id, a);
        addr = a;
    }
}

auto sysv_shmem_attachment::detach() noexcept -> bool {
    if (addr == nullptr)
        return true;
    bool ret = false;
    errno = 0;
    if (::shmdt(addr) != 0) {
        int e = errno;
        auto msg = posix::strerror(e);
        spdlog::error("shmdt: addr {}: {} ({})", addr, msg, e);
    } else {
        spdlog::info("shmdt: addr {}: success", addr);
        ret = true;
    }
    addr = nullptr;
    return ret;
}

TEST_CASE("sysv_shmem_attachment") {
    SUBCASE("default instance") {
        sysv_shmem_attachment att;
        CHECK_FALSE(att.is_valid());
        CHECK(att.address() == nullptr);

        SUBCASE("let destructor clean up") {}

        SUBCASE("explicitly detach") {
            REQUIRE(att.detach());
            CHECK(att.detach()); // Idempotent
        }
    }

    GIVEN("a valid shm id") {
        auto shmid = create_sysv_shmem_id(IPC_PRIVATE, 16384, false);
        REQUIRE(shmid.is_valid());

        SUBCASE("create attachment") {
            auto att = sysv_shmem_attachment(shmid.id());
            CHECK(att.is_valid());
            CHECK(att.address() != nullptr);

            SUBCASE("create second attachment") {
                auto att2 = sysv_shmem_attachment(shmid.id());
                CHECK(att2.is_valid());
            }

            // NOLINTBEGIN(bugprone-use-after-move)
            SUBCASE("move-construct") {
                void *addr = att.address();
                sysv_shmem_attachment const other(std::move(att));
                CHECK_FALSE(att.is_valid());
                CHECK(other.is_valid());
                CHECK(other.address() == addr);
            }

            SUBCASE("move-assign") {
                void *addr = att.address();
                sysv_shmem_attachment other;
                other = std::move(att);
                CHECK_FALSE(att.is_valid());
                CHECK(other.is_valid());
                CHECK(other.address() == addr);
            }
            // NOLINTEND(bugprone-use-after-move)

            SUBCASE("explicitly detach") {
                REQUIRE(att.detach());
                CHECK(att.detach()); // Idempotent
            }
        }
    }
}

} // namespace internal

auto create_sysv_shmem(std::size_t size, bool use_huge_pages) noexcept
    -> sysv_shmem {
    return sysv_shmem(internal::create_sysv_shmem_id(IPC_PRIVATE, size, false,
                                                     use_huge_pages));
}

auto create_sysv_shmem(int key, std::size_t size, bool force,
                       bool use_huge_pages) noexcept -> sysv_shmem {
    return sysv_shmem(
        internal::create_sysv_shmem_id(key, size, force, use_huge_pages));
}

TEST_CASE("create_sysv_shmem") {
    auto shm = create_sysv_shmem(4096);
    CHECK(shm.is_valid());
    CHECK(shm.id() >= 0);
    CHECK(shm.address() != nullptr);
    CHECK(shm.remove());
    CHECK(shm.detach());
}

} // namespace partake::daemon

#endif // _WIN32
