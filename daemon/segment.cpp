/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "segment.hpp"

#include "overloaded.hpp"
#include "shmem_mmap.hpp"
#include "shmem_sysv.hpp"
#include "shmem_win32.hpp"
#include "testing.hpp"

#include <doctest.h>

#include <cassert>
#include <exception>

namespace partake::daemon {

namespace {

class unsupported_segment final : public internal::segment_impl {
    std::size_t siz;

  public:
    explicit unsupported_segment(std::size_t size) noexcept : siz(size) {}

    template <typename Cfg>
    explicit unsupported_segment([[maybe_unused]] Cfg const &cfg,
                                 std::size_t size) noexcept
        : siz(size) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return false;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return siz;
    }

    [[nodiscard]] auto spec() const noexcept -> segment_spec override {
        assert(false);
        std::terminate();
    }
};

#ifndef _WIN32

class posix_mmap_segment final : public internal::segment_impl {
    mmap_shmem shm;

  public:
    explicit posix_mmap_segment(posix_mmap_segment_config const &cfg,
                                std::size_t size) noexcept
        : shm(cfg.name.empty()
                  ? create_posix_mmap_shmem(size)
                  : create_posix_mmap_shmem(cfg.name, size, cfg.force)) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return shm.is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return shm.size();
    }

    [[nodiscard]] auto spec() const noexcept -> segment_spec override {
        return {posix_mmap_segment_spec{shm.name()}, size()};
    }
};

class file_mmap_segment final : public internal::segment_impl {
    mmap_shmem shm;

  public:
    explicit file_mmap_segment(file_mmap_segment_config const &cfg,
                               std::size_t size) noexcept
        : shm([&]() {
              if (cfg.filename.empty())
                  return create_file_mmap_shmem(size);
              std::error_code ec;
              auto canon = std::filesystem::weakly_canonical(cfg.filename, ec);
              if (ec) {
                  spdlog::error("{}: Cannot get canonical path: {} ({})",
                                cfg.filename, ec.message(), ec.value());
                  return mmap_shmem();
              }
              return create_file_mmap_shmem(canon, size, cfg.force);
          }()) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return shm.is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return shm.size();
    }

    [[nodiscard]] auto spec() const noexcept -> segment_spec override {
        return {file_mmap_segment_spec{shm.name()}, size()};
    }
};

class sysv_segment final : public internal::segment_impl {
    sysv_shmem shm;

  public:
    explicit sysv_segment(sysv_segment_config const &cfg,
                          std::size_t size) noexcept
        : shm(cfg.key == 0 ? create_sysv_shmem(size, cfg.use_huge_pages)
                           : create_sysv_shmem(cfg.key, size, cfg.force,
                                               cfg.use_huge_pages)) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return shm.is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return shm.size();
    }

    [[nodiscard]] auto spec() const noexcept -> segment_spec override {
        return {sysv_segment_spec{shm.id()}, size()};
    }
};

using win32_segment = unsupported_segment;

#else // _WIN32

using posix_mmap_segment = unsupported_segment;
using file_mmap_segment = unsupported_segment;
using sysv_segment = unsupported_segment;

class win32_segment final : public internal::segment_impl {
    std::string mapping_name;
    win32_shmem shm;
    bool large_pages;

  public:
    explicit win32_segment(win32_segment_config const &cfg,
                           std::size_t size) noexcept
        : mapping_name(cfg.name.empty() ? generate_win32_file_mapping_name()
                                        : cfg.name),
          shm([&]() {
              if (cfg.filename.empty()) {
                  return create_win32_shmem(mapping_name, size,
                                            cfg.use_large_pages);
              }
              std::error_code ec;
              auto canon = std::filesystem::weakly_canonical(cfg.filename, ec);
              if (ec) {
                  spdlog::error("{}: Cannot get canonical path: {} ({})",
                                cfg.filename, ec.message(), ec.value());
                  return win32_shmem();
              }
              return create_win32_file_shmem(canon, mapping_name, size,
                                             cfg.force, cfg.use_large_pages);
          }()),
          large_pages(cfg.use_large_pages) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool override {
        return shm.is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t override {
        return shm.size();
    }

    [[nodiscard]] auto spec() const noexcept -> segment_spec override {
        return {win32_segment_spec{mapping_name, large_pages}, size()};
    }
};

#endif // _WIN32

} // namespace

segment::segment() noexcept : impl(std::make_unique<unsupported_segment>(0)) {}

segment::segment(segment_config const &config) noexcept
    : impl(std::visit(
          internal::overloaded{
              [&config](
                  posix_mmap_segment_config const &cfg) noexcept -> impl_ptr {
                  return std::make_unique<posix_mmap_segment>(cfg,
                                                              config.size);
              },
              [&config](
                  file_mmap_segment_config const &cfg) noexcept -> impl_ptr {
                  return std::make_unique<file_mmap_segment>(cfg, config.size);
              },
              [&config](sysv_segment_config const &cfg) noexcept -> impl_ptr {
                  return std::make_unique<sysv_segment>(cfg, config.size);
              },
              [&config](win32_segment_config const &cfg) noexcept -> impl_ptr {
                  return std::make_unique<win32_segment>(cfg, config.size);
              },
          },
          config.method)) {}

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

        SUBCASE("create, no-force") {
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

            SUBCASE("create, no-force") {
                auto const conf =
                    segment_config{win32_segment_config{{}, name}, 8192};
                segment const seg(conf);
                REQUIRE_FALSE(seg.is_valid());
            }

            SUBCASE("create, force") {
                auto const conf =
                    segment_config{win32_segment_config{{}, name, true}, 8192};
                segment const seg(conf);
                // There is no "force" for file mapping creation, so this still
                // fails.
                REQUIRE_FALSE(seg.is_valid());
            }
        }
    }

    SUBCASE("create with generated name") {
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

        SUBCASE("create named, no-force") {
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

        SUBCASE("create, no-force") {
            auto const conf = segment_config{
                win32_segment_config{file.path().string(), {}}, 8192};
            segment const seg(conf);
            CHECK_FALSE(seg.is_valid());
        }

        SUBCASE("create, force") {
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
        auto const name = "/partake-test-" + random_string(10);

        SUBCASE("create named, no-force") {
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

            SUBCASE("create named, no-force") {
                auto const conf =
                    segment_config{posix_mmap_segment_config{name}, 8192};
                segment const seg(conf);
                CHECK_FALSE(seg.is_valid());
            }

            SUBCASE("create named, force") {
                auto const conf = segment_config{
                    posix_mmap_segment_config{name, true}, 8192};
                segment const seg(conf);
                CHECK(seg.is_valid());
                CHECK(seg.size() >= 8192);
            }
        }
    }

    SUBCASE("create with generated name") {
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

        SUBCASE("create named, no-force") {
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

            SUBCASE("create named, no-force") {
                auto const conf = segment_config{
                    file_mmap_segment_config{path.string()}, 8192};
                segment const seg(conf);
                CHECK_FALSE(seg.is_valid());
            }

            SUBCASE("create named, force") {
                auto const conf = segment_config{
                    file_mmap_segment_config{path.string(), true}, 8192};
                segment const seg(conf);
                CHECK(seg.is_valid());
                CHECK(seg.size() >= 8192);
            }
        }
    }

    SUBCASE("create with generated name") {
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

        SUBCASE("create with key, non-preexisting") {
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

        SUBCASE("create with key, preexisting, no-force") {
            auto const conf =
                segment_config{sysv_segment_config{key, false}, 8192};
            segment const seg(conf);
            CHECK_FALSE(seg.is_valid());
        }

        SUBCASE("create with key, preexisting, force") {
            auto const conf =
                segment_config{sysv_segment_config{key, true}, 8192};
            segment const seg(conf);
            CHECK(seg.is_valid());
            CHECK(seg.size() >= 8192);
        }
    }

    SUBCASE("create with auto-selected key") {
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
