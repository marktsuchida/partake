/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "segment.hpp"

#include "random.hpp"
#include "shmem_mmap.hpp"
#include "shmem_sysv.hpp"
#include "shmem_win32.hpp"
#include "testing.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <variant>

namespace partake::daemon {

#ifdef _WIN32

TEST_CASE("segment: invalid on Win32") {
    auto const conf_posix = segment_config{posix_mmap_segment_config{}, 8192};
    CHECK_FALSE(segment(conf_posix).is_valid());
    auto const conf_mmap = segment_config{file_mmap_segment_config{}, 8192};
    CHECK_FALSE(segment(conf_mmap).is_valid());
    auto const conf_sysv = segment_config{sysv_segment_config{}, 8192};
    CHECK_FALSE(segment(conf_sysv).is_valid());
}

TEST_CASE("segment: win32 system paging file") {
    GIVEN("unique mapping name") {
        auto const name = "Local\\partake-test-" + random_string(10);

        SECTION("create, no-force") {
            auto const conf =
                segment_config{win32_segment_config{{}, name}, 8192};
            segment const seg(conf);
            REQUIRE(seg.is_valid());
            CHECK(seg.size() >= 8192);
            auto const spec = seg.spec();
            REQUIRE(std::holds_alternative<win32_segment_spec>(spec.spec));
            auto win32_spec = std::get<win32_segment_spec>(spec.spec);
            CHECK(win32_spec.name == name);
            CHECK_FALSE(win32_spec.use_large_pages);
        }

        GIVEN("preexisting") {
            auto preexisting = create_win32_shmem(name, 4096);

            SECTION("create, no-force") {
                auto const conf =
                    segment_config{win32_segment_config{{}, name}, 8192};
                segment const seg(conf);
                REQUIRE_FALSE(seg.is_valid());
            }

            SECTION("create, force") {
                auto const conf =
                    segment_config{win32_segment_config{{}, name, true}, 8192};
                segment const seg(conf);
                // There is no "force" for file mapping creation, so this still
                // fails.
                REQUIRE_FALSE(seg.is_valid());
            }
        }
    }

    SECTION("create with generated name") {
        auto const conf = segment_config{win32_segment_config{}, 8192};
        segment const seg(conf);
        CHECK(seg.is_valid());
        CHECK(seg.size() >= 8192);
        auto const spec = seg.spec();
        REQUIRE(std::holds_alternative<win32_segment_spec>(spec.spec));
        auto win32_spec = std::get<win32_segment_spec>(spec.spec);
        CHECK(win32_spec.name.size() > std::string("Local\\").size());
        CHECK(win32_spec.name.substr(0, std::string("Local\\").size()) ==
              "Local\\");
        CHECK_FALSE(win32_spec.use_large_pages);
    }
}

TEST_CASE("segment: win32 file") {
    testing::tempdir const td;

    GIVEN("unique filename") {
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));

        SECTION("create named, no-force") {
            auto const conf =
                segment_config{win32_segment_config{path.string(), {}}, 8192};
            segment const seg(conf);
            REQUIRE(seg.is_valid());
            CHECK(seg.size() >= 8192);
            auto const spec = seg.spec();
            REQUIRE(std::holds_alternative<win32_segment_spec>(spec.spec));
            auto win32_spec = std::get<win32_segment_spec>(spec.spec);
            CHECK_FALSE(win32_spec.name.empty());
            CHECK_FALSE(win32_spec.use_large_pages);
        }
    }

    GIVEN("preexisting file") {
        auto file = testing::unique_file_with_data(
            td.path(), testing::make_test_filename(__FILE__, __LINE__), {});

        SECTION("create, no-force") {
            auto const conf = segment_config{
                win32_segment_config{file.path().string(), {}}, 8192};
            segment const seg(conf);
            CHECK_FALSE(seg.is_valid());
        }

        SECTION("create, force") {
            auto const conf = segment_config{
                win32_segment_config{file.path().string(), {}, true}, 8192};
            segment const seg(conf);
            REQUIRE(seg.is_valid());
            CHECK(seg.size() >= 8192);
            auto const spec = seg.spec();
            REQUIRE(std::holds_alternative<win32_segment_spec>(spec.spec));
            auto win32_spec = std::get<win32_segment_spec>(spec.spec);
            CHECK_FALSE(win32_spec.name.empty());
            CHECK_FALSE(win32_spec.use_large_pages);
        }
    }
}

#else // _WIN32

TEST_CASE("segment: invalid on non-Win32") {
    auto const conf_win32 = segment_config{win32_segment_config{}, 8192};
    CHECK_FALSE(segment(conf_win32).is_valid());
}

TEST_CASE("segment: posix mmap") {
    GIVEN("unique shmem name") {
        auto const name = "/partake-test-" + common::random_string(10);

        SECTION("create named, no-force") {
            auto const conf =
                segment_config{posix_mmap_segment_config{name}, 8192};
            segment const seg(conf);
            REQUIRE(seg.is_valid());
            CHECK(seg.size() >= 8192);
            auto const spec = seg.spec();
            REQUIRE(
                std::holds_alternative<posix_mmap_segment_spec>(spec.spec));
            auto mmap_spec = std::get<posix_mmap_segment_spec>(spec.spec);
            CHECK(mmap_spec.name == name);
        }

        GIVEN("preexisting") {
            auto preexisting = create_posix_mmap_shmem(name, 4096, false);
            preexisting.unmap(); // But don't unlink

            SECTION("create named, no-force") {
                auto const conf =
                    segment_config{posix_mmap_segment_config{name}, 8192};
                segment const seg(conf);
                CHECK_FALSE(seg.is_valid());
            }

            SECTION("create named, force") {
                auto const conf = segment_config{
                    posix_mmap_segment_config{name, true}, 8192};
                segment const seg(conf);
                CHECK(seg.is_valid());
                CHECK(seg.size() >= 8192);
            }
        }
    }

    SECTION("create with generated name") {
        auto const conf = segment_config{posix_mmap_segment_config{}, 8192};
        segment const seg(conf);
        CHECK(seg.is_valid());
        CHECK(seg.size() >= 8192);
        auto const spec = seg.spec();
        REQUIRE(std::holds_alternative<posix_mmap_segment_spec>(spec.spec));
        auto mmap_spec = std::get<posix_mmap_segment_spec>(spec.spec);
        CHECK(mmap_spec.name.size() > 1);
        CHECK(mmap_spec.name[0] == '/');
    }
}

TEST_CASE("segment: file mmap") {
    GIVEN("unique filename") {
        testing::tempdir const td;
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));

        SECTION("create named, no-force") {
            auto const conf =
                segment_config{file_mmap_segment_config{path.string()}, 8192};
            segment const seg(conf);
            CHECK(seg.is_valid());
            CHECK(seg.size() >= 8192);
            auto const spec = seg.spec();
            REQUIRE(std::holds_alternative<file_mmap_segment_spec>(spec.spec));
            auto file_spec = std::get<file_mmap_segment_spec>(spec.spec);
            CHECK(file_spec.filename ==
                  std::filesystem::weakly_canonical(path));
        }

        GIVEN("preexisting") {
            auto preexisting =
                create_file_mmap_shmem(path.string(), 4096, false);
            preexisting.unmap(); // But don't unlink

            SECTION("create named, no-force") {
                auto const conf = segment_config{
                    file_mmap_segment_config{path.string()}, 8192};
                segment const seg(conf);
                CHECK_FALSE(seg.is_valid());
            }

            SECTION("create named, force") {
                auto const conf = segment_config{
                    file_mmap_segment_config{path.string(), true}, 8192};
                segment const seg(conf);
                CHECK(seg.is_valid());
                CHECK(seg.size() >= 8192);
            }
        }
    }

    SECTION("create with generated name") {
        auto const conf = segment_config{file_mmap_segment_config{}, 8192};
        segment const seg(conf);
        CHECK(seg.is_valid());
        CHECK(seg.size() >= 8192);
        auto const spec = seg.spec();
        REQUIRE(std::holds_alternative<file_mmap_segment_spec>(spec.spec));
        auto file_spec = std::get<file_mmap_segment_spec>(spec.spec);
        CHECK(file_spec.filename.size() > 1);
        CHECK(file_spec.filename[0] == '/');
    }
}

TEST_CASE("segment: sysv") {
    GIVEN("known, preexisting key") {
        int key = 0;
        sysv_shmem preexisting;
        while (not preexisting.is_valid()) {
            ++key;
            preexisting = create_sysv_shmem(key, 4096);
        }
        preexisting.detach(); // But don't remove

        SECTION("create with key, non-preexisting") {
            // Establish a known-unused key by removing.
            preexisting.remove();

            auto const conf = segment_config{sysv_segment_config{key}, 8192};
            segment const seg(conf);
            CHECK(seg.is_valid());
            CHECK(seg.size() >= 8192);
            auto const spec = seg.spec();
            REQUIRE(std::holds_alternative<sysv_segment_spec>(spec.spec));
            auto sysv_spec = std::get<sysv_segment_spec>(spec.spec);
            CHECK(sysv_spec.shm_id >= 0);
        }

        SECTION("create with key, preexisting, no-force") {
            auto const conf =
                segment_config{sysv_segment_config{key, false}, 8192};
            segment const seg(conf);
            CHECK_FALSE(seg.is_valid());
        }

        SECTION("create with key, preexisting, force") {
            auto const conf =
                segment_config{sysv_segment_config{key, true}, 8192};
            segment const seg(conf);
            CHECK(seg.is_valid());
            CHECK(seg.size() >= 8192);
        }
    }

    SECTION("create with auto-selected key") {
        auto const conf = segment_config{sysv_segment_config{}, 8192};
        segment const seg(conf);
        CHECK(seg.is_valid());
        CHECK(seg.size() >= 8192);
        auto const spec = seg.spec();
        REQUIRE(std::holds_alternative<sysv_segment_spec>(spec.spec));
        auto sysv_spec = std::get<sysv_segment_spec>(spec.spec);
        CHECK(sysv_spec.shm_id >= 0);
    }
}

#endif // _WIN32

} // namespace partake::daemon
