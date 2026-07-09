/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_sysv.hpp"

#ifndef _WIN32

#include "page_size.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sys/shm.h>

namespace partake::daemon {

namespace internal {

TEST_CASE("sysv_shmem_id") {
    // NOLINTBEGIN(readability-magic-numbers)

    SECTION("default instance") {
        sysv_shmem_id shmid;
        CHECK_FALSE(shmid.is_valid());
        CHECK(shmid.id() == -1);
        CHECK(shmid.size() == 0);

        SECTION("let destructor clean up") {}

        SECTION("explicitly remove") {
            REQUIRE(shmid.remove());
            CHECK(shmid.remove()); // Idempotent
        }
    }

    SECTION("create id by finding non-existent key") {
        key_t key = 0;
        sysv_shmem_id shmid;
        while (not shmid.is_valid()) {
            ++key;
            shmid = create_sysv_shmem_id(key, 100);
        }
        CHECK(shmid.id() >= 0);
        CHECK(shmid.size() == page_size());

        SECTION("create with existing key, no-force") {
            SECTION("same size") {
                auto shmid2 = create_sysv_shmem_id(key, 100, false);
                CHECK_FALSE(shmid2.is_valid());
                CHECK(shmid2.id() == -1);
                CHECK(shmid2.size() == 0);
            }

            SECTION("larger size") {
                auto shmid2 = create_sysv_shmem_id(key, 100, false);
                CHECK_FALSE(shmid2.is_valid());
                CHECK(shmid2.id() == -1);
                CHECK(shmid2.size() == 0);
            }
        }

        SECTION("create with existing key, force") {
            SECTION("same size") {
                auto shmid2 = create_sysv_shmem_id(key, page_size(), true);
                CHECK(shmid2.is_valid());
                CHECK(shmid2.id() >= 0);
                CHECK(shmid2.size() == page_size());
            }

            SECTION("larger size") {
                auto shmid2 = create_sysv_shmem_id(key, 2 * page_size(), true);
                CHECK(shmid2.is_valid());
                CHECK(shmid2.id() >= 0);
                CHECK(shmid2.size() == 2 * page_size());
            }
        }

        // NOLINTBEGIN(bugprone-use-after-move)
        SECTION("move-construct") {
            auto id = shmid.id();
            sysv_shmem_id const other(std::move(shmid));
            CHECK_FALSE(shmid.is_valid());
            CHECK(other.is_valid());
            CHECK(other.id() == id);
            CHECK(other.size() == page_size());
        }

        SECTION("move-assign") {
            auto id = shmid.id();
            sysv_shmem_id other;
            other = std::move(shmid);
            CHECK_FALSE(shmid.is_valid());
            CHECK(other.is_valid());
            CHECK(other.id() == id);
            CHECK(other.size() == page_size());
        }
        // NOLINTEND(bugprone-use-after-move)
    }

    SECTION("create id with IPC_PRIVATE") {
        auto shmid = create_sysv_shmem_id(IPC_PRIVATE, 100, false);
        CHECK(shmid.is_valid());
        CHECK(shmid.id() >= 0);
        CHECK(shmid.size() == page_size());

        SECTION("let destructor clean up") {}

        SECTION("explicitly remove") {
            REQUIRE(shmid.remove());
            CHECK(shmid.remove()); // Idempotent
        }
    }

    // NOLINTEND(readability-magic-numbers)
}

TEST_CASE("sysv_shmem_attachment") {
    SECTION("default instance") {
        sysv_shmem_attachment att;
        CHECK_FALSE(att.is_valid());
        CHECK(att.address() == nullptr);

        SECTION("let destructor clean up") {}

        SECTION("explicitly detach") {
            REQUIRE(att.detach());
            CHECK(att.detach()); // Idempotent
        }
    }

    GIVEN("a valid shm id") {
        auto shmid = create_sysv_shmem_id(IPC_PRIVATE, 16384, false);
        REQUIRE(shmid.is_valid());

        SECTION("create attachment") {
            auto att = sysv_shmem_attachment(shmid.id());
            CHECK(att.is_valid());
            CHECK(att.address() != nullptr);

            SECTION("create second attachment") {
                auto att2 = sysv_shmem_attachment(shmid.id());
                CHECK(att2.is_valid());
            }

            // NOLINTBEGIN(bugprone-use-after-move)
            SECTION("move-construct") {
                void *addr = att.address();
                sysv_shmem_attachment const other(std::move(att));
                CHECK_FALSE(att.is_valid());
                CHECK(other.is_valid());
                CHECK(other.address() == addr);
            }

            SECTION("move-assign") {
                void *addr = att.address();
                sysv_shmem_attachment other;
                other = std::move(att);
                CHECK_FALSE(att.is_valid());
                CHECK(other.is_valid());
                CHECK(other.address() == addr);
            }
            // NOLINTEND(bugprone-use-after-move)

            SECTION("explicitly detach") {
                REQUIRE(att.detach());
                CHECK(att.detach()); // Idempotent
            }
        }
    }
}

} // namespace internal

TEST_CASE("create_sysv_shmem") {
    // NOLINTNEXTLINE(readability-magic-numbers)
    auto shm = create_sysv_shmem(100);
    CHECK(shm.is_valid());
    CHECK(shm.id() >= 0);
    CHECK(shm.address() != nullptr);
    CHECK(shm.size() == page_size());
    CHECK(shm.remove());
    CHECK(shm.detach());
}

} // namespace partake::daemon

#endif // _WIN32
