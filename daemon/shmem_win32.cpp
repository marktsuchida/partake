/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_win32.hpp"

#ifdef _WIN32

#include "page_size.hpp"
#include "random.hpp"
#include "sizes.hpp"
#include "testing.hpp"
#include "win32.hpp"

#include <doctest.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <Windows.h>

namespace partake::daemon {

namespace {

auto add_lock_memory_privilege() -> bool {
    win32::win32_handle const h_token(
        [] {
            HANDLE h = INVALID_HANDLE_VALUE;
            if (OpenProcessToken(GetCurrentProcess(),
                                 TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES,
                                 &h) == 0) {
                auto err = GetLastError();
                auto msg = win32::strerror(err);
                spdlog::error("OpenProcessToken: {} ({})", msg, err);
                return INVALID_HANDLE_VALUE;
            }
            return h;
        }(),
        spdlog::default_logger());
    if (not h_token.is_valid())
        return false;

    LUID lock_mem_luid;
    if (LookupPrivilegeValueA(nullptr, SE_LOCK_MEMORY_NAME, &lock_mem_luid) ==
        0) {
        auto err = GetLastError();
        auto msg = win32::strerror(err);
        spdlog::error("LookupPrivilegeValue: {} ({})", msg, err);
        return false;
    }

    TOKEN_PRIVILEGES privileges;
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = lock_mem_luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    bool ok = AdjustTokenPrivileges(h_token.get(), FALSE, &privileges,
                                    sizeof(privileges), nullptr, nullptr) != 0;
    // ERROR_NOT_ALL_ASSIGNED (only) may be returned even if ok is true.
    auto err = GetLastError();
    if (not ok || err == ERROR_NOT_ALL_ASSIGNED) {
        auto msg = win32::strerror(err);
        spdlog::error("AdjustTokenPrivileges: {}: {} ({})",
                      SE_LOCK_MEMORY_NAME, msg, err);
        return false;
    }
    spdlog::info("AdjustTokenPrivileges: {}: success", SE_LOCK_MEMORY_NAME);
    return true;
}

TEST_CASE("add_lock_memory_privilege") { CHECK(add_lock_memory_privilege()); }

auto create_autodeleted_file(std::filesystem::path const &path, bool force)
    -> win32::win32_handle {
    auto h_file = win32::win32_handle(
        CreateFileA(path.string().c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                    nullptr, force ? CREATE_ALWAYS : CREATE_NEW,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                    nullptr),
        spdlog::default_logger());
    if (not h_file.is_valid()) {
        auto err = GetLastError();
        auto msg = win32::strerror(err);
        spdlog::error("CreateFile: {}: {} ({})", path.string(), msg, err);
    } else {
        spdlog::info("CreateFile: {}: success, handle {}", path.string(),
                     h_file.get());
    }
    return h_file;
}

TEST_CASE("create_autodeleted_file") {
    testing::tempdir const td;

    GIVEN("unique file name") {
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));

        SUBCASE("create, no-force") {
            auto h = create_autodeleted_file(path, false);
            CHECK(h.is_valid());
        }

        SUBCASE("create, force") {
            auto h = create_autodeleted_file(path, true);
            CHECK(h.is_valid());

            SUBCASE("let destructor clean up") {}

            SUBCASE("explicitly close") { CHECK(h.close()); }
        }
    }

    GIVEN("preexisting file") {
        auto file = testing::unique_file_with_data(
            td.path(), testing::make_test_filename(__FILE__, __LINE__), {});

        SUBCASE("create, no-force") {
            auto h = create_autodeleted_file(file.path(), false);
            CHECK_FALSE(h.is_valid());
        }

        SUBCASE("create, force") {
            auto h = create_autodeleted_file(file.path(), true);
            CHECK(h.is_valid());
        }
    }
}

auto create_file_mapping(win32::win32_handle const &file_handle,
                         std::string const &name, std::size_t size,
                         bool use_large_pages = false) -> win32::win32_handle {
    if (name.empty() || size == 0)
        return {};
    if (use_large_pages)
        add_lock_memory_privilege(); // Ignore errors; let next step fail.
    HANDLE raw_handle = CreateFileMappingA(
        file_handle.get(), nullptr,
        PAGE_READWRITE | SEC_COMMIT | (use_large_pages ? SEC_LARGE_PAGES : 0),
        sizeof(std::size_t) > 4 ? size >> 32 : 0, size & UINT_MAX,
        name.c_str());
    // Docs say return value is NULL (not INVALID_HANDLE_VALUE) on failure.
    auto h_mapping =
        raw_handle == nullptr
            ? win32::win32_handle()
            : win32::win32_handle(raw_handle, spdlog::default_logger());
    // Return value does not indicate failure when the mapping already exists,
    // but GetLastError() does.
    auto err = GetLastError();
    if (not h_mapping.is_valid() || err == ERROR_ALREADY_EXISTS) {
        auto msg = win32::strerror(err);
        spdlog::error("CreateFileMapping: {}: {} ({})", name, msg, err);
        return {};
    }
    spdlog::info("CreateFileMapping: {}: success, handle {}", name,
                 h_mapping.get());
    return h_mapping;
}

TEST_CASE("create_file_mapping") {
    auto const name = "Local\\partake-test-" + random_string(10);

    GIVEN("a file handle") {
        testing::tempdir const td;
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));
        auto h_file = create_autodeleted_file(path, false);
        CHECK(h_file.is_valid());

        SUBCASE("create mapping") {
            auto h_mapping = create_file_mapping(h_file, name, 4096);
            CHECK(h_mapping.is_valid());

            SUBCASE("create with existing name") {
                auto h_mapping_2 = create_file_mapping(h_file, name, 4096);
                CHECK_FALSE(h_mapping_2.is_valid());
            }
        }
    }

    SUBCASE("create with system paging file") {
        auto h_mapping = create_file_mapping({}, name, 4096);
        CHECK(h_mapping.is_valid());

        SUBCASE("create with existing name") {
            auto h_mapping_2 = create_file_mapping({}, name, 4096);
            CHECK_FALSE(h_mapping_2.is_valid());
        }
    }

    SUBCASE("try to create with impractical size") {
        auto h_mapping = create_file_mapping(
            {}, name, std::numeric_limits<std::size_t>::max());
        CHECK_FALSE(h_mapping.is_valid());
    }
}

} // namespace

namespace internal {

win32_map_view::win32_map_view(win32::win32_handle const &h_mapping,
                               std::size_t size, bool use_large_pages)
    : addr(
          h_mapping.is_valid()
              ? MapViewOfFile(h_mapping.get(),
                              FILE_MAP_READ | FILE_MAP_WRITE |
                                  (use_large_pages ? FILE_MAP_LARGE_PAGES : 0),
                              0, 0, size)
              : nullptr),
      siz(size) {
    if (h_mapping.is_valid() && addr == nullptr) {
        auto err = GetLastError();
        auto msg = win32::strerror(err);
        spdlog::error("MapViewOfFile: {}: {} ({})", h_mapping.get(), msg, err);
    } else {
        spdlog::info("MapViewOfFile: {}: success; addr {}", h_mapping.get(),
                     addr);
    }
}

void win32_map_view::unmap() {
    if (addr != nullptr) {
        if (UnmapViewOfFile(addr) == 0) {
            auto err = GetLastError();
            auto msg = win32::strerror(err);
            spdlog::error("UnmapViewOfFile: addr {}: {} ({})", addr, msg, err);
        } else {
            spdlog::info("UnmapViewOfFile: addr {}: success", addr);
        }
    }
}

TEST_CASE("win32_map_view") {
    SUBCASE("default instance") {
        win32_map_view const mv;
        CHECK_FALSE(mv.is_valid());
        CHECK(mv.address() == nullptr);
        CHECK(mv.size() == 0);
    }

    GIVEN("a file mapping") {
        auto const name = "Local\\partake-test-" + random_string(10);
        auto h_mapping = create_file_mapping({}, name, 4096);
        CHECK(h_mapping.is_valid());

        SUBCASE("create map view") {
            auto mv = win32_map_view(h_mapping, 4096);
            CHECK(mv.is_valid());
            CHECK(mv.address() != nullptr);
            void *addr = mv.address();

            // NOLINTBEGIN(bugprone-use-after-move)
            SUBCASE("move-construct") {
                win32_map_view const other(std::move(mv));
                CHECK_FALSE(mv.is_valid());
                CHECK(other.is_valid());
                CHECK(other.address() == addr);
            }

            SUBCASE("move-assign") {
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

auto create_win32_shmem(std::string const &mapping_name, std::size_t size,
                        bool use_large_pages) -> win32_shmem {
    auto const granularity = use_large_pages ? large_page_minimum()
                                             : system_allocation_granularity();
    if (not round_up_or_check_size(size, granularity))
        return {};

    return win32_shmem(
        {}, create_file_mapping({}, mapping_name, size, use_large_pages), size,
        use_large_pages);
}

TEST_CASE("create_win32_shmem") {
    auto shm = create_win32_shmem(generate_win32_file_mapping_name(), 100);
    CHECK(shm.is_valid());
    CHECK(shm.address() != nullptr);
    CHECK(shm.size() == system_allocation_granularity());
}

auto create_win32_file_shmem(std::filesystem::path const &path,
                             std::string const &mapping_name, std::size_t size,
                             bool force, bool use_large_pages) -> win32_shmem {
    auto const granularity = use_large_pages ? large_page_minimum()
                                             : system_allocation_granularity();
    if (not round_up_or_check_size(size, granularity))
        return {};

    auto h_file = create_autodeleted_file(path, force);
    if (not h_file.is_valid())
        return {};
    auto h_mapping =
        create_file_mapping(h_file, mapping_name, size, use_large_pages);
    return win32_shmem(std::move(h_file), std::move(h_mapping), size,
                       use_large_pages);
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
