/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_win32.hpp"

#ifdef _WIN32

#include "page_size.hpp"
#include "random.hpp"
#include "testing.hpp"
#include "win32.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

#include <Windows.h>

namespace partake::daemon {

namespace internal {

TEST_CASE("add_lock_memory_privilege") { CHECK(add_lock_memory_privilege()); }

TEST_CASE("create_autodeleted_file") {
    testing::tempdir const td;

    GIVEN("unique file name") {
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));

        SECTION("create, no-force") {
            auto h = create_autodeleted_file(path, false);
            CHECK(h.is_valid());
        }

        SECTION("create, force") {
            auto h = create_autodeleted_file(path, true);
            CHECK(h.is_valid());

            SECTION("let destructor clean up") {}

            SECTION("explicitly close") { CHECK(h.close()); }
        }
    }

    GIVEN("preexisting file") {
        auto file = testing::unique_file_with_data(
            td.path(), testing::make_test_filename(__FILE__, __LINE__), {});

        SECTION("create, no-force") {
            auto h = create_autodeleted_file(file.path(), false);
            CHECK_FALSE(h.is_valid());
        }

        SECTION("create, force") {
            auto h = create_autodeleted_file(file.path(), true);
            CHECK(h.is_valid());
        }
    }
}

TEST_CASE("create_file_mapping") {
    auto const name = "Local\\partake-test-" + random_string(10);

    GIVEN("a file handle") {
        testing::tempdir const td;
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));
        auto h_file = create_autodeleted_file(path, false);
        CHECK(h_file.is_valid());

        SECTION("create mapping") {
            auto h_mapping = create_file_mapping(h_file, name, 4096);
            CHECK(h_mapping.is_valid());

            SECTION("create with existing name") {
                auto h_mapping_2 = create_file_mapping(h_file, name, 4096);
                CHECK_FALSE(h_mapping_2.is_valid());
            }
        }
    }

    SECTION("create with system paging file") {
        auto h_mapping = create_file_mapping({}, name, 4096);
        CHECK(h_mapping.is_valid());

        SECTION("create with existing name") {
            auto h_mapping_2 = create_file_mapping({}, name, 4096);
            CHECK_FALSE(h_mapping_2.is_valid());
        }
    }

    SECTION("try to create with impractical size") {
        auto h_mapping = create_file_mapping(
            {}, name, std::numeric_limits<std::size_t>::max());
        CHECK_FALSE(h_mapping.is_valid());
    }
}

TEST_CASE("win32_map_view") {
    SECTION("default instance") {
        win32_map_view const mv;
        CHECK_FALSE(mv.is_valid());
        CHECK(mv.address() == nullptr);
        CHECK(mv.size() == 0);
    }

    GIVEN("a file mapping") {
        auto const name = "Local\\partake-test-" + random_string(10);
        auto h_mapping = create_file_mapping({}, name, 4096);
        CHECK(h_mapping.is_valid());

        SECTION("create map view") {
            auto mv = win32_map_view(h_mapping, 4096);
            CHECK(mv.is_valid());
            CHECK(mv.address() != nullptr);
            void *addr = mv.address();

            // NOLINTBEGIN(bugprone-use-after-move)
            SECTION("move-construct") {
                win32_map_view const other(std::move(mv));
                CHECK_FALSE(mv.is_valid());
                CHECK(other.is_valid());
                CHECK(other.address() == addr);
            }

            SECTION("move-assign") {
                win32_map_view other;
                other = std::move(mv);
                CHECK_FALSE(mv.is_valid());
                CHECK(other.is_valid());
                CHECK(other.address() == addr);
            }
            // NOLINTEND(bugprone-use-after-move)
        }
    }
}

} // namespace internal

TEST_CASE("create_win32_shmem") {
    auto shm = create_win32_shmem(generate_win32_file_mapping_name(), 100);
    CHECK(shm.is_valid());
    CHECK(shm.address() != nullptr);
    CHECK(shm.size() == system_allocation_granularity());
}

TEST_CASE("create_win32_file_shmem") {
    testing::tempdir const td;
    auto path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));

    auto shm =
        create_win32_file_shmem(path, generate_win32_file_mapping_name(), 100);
    CHECK(shm.is_valid());
    CHECK(shm.address() != nullptr);
    CHECK(shm.size() == system_allocation_granularity());
}

} // namespace partake::daemon

#endif // _WIN32
