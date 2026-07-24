/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_win32.hpp"

#ifdef _WIN32

#include "page_size.hpp"
#include "sizes.hpp"
#include "win32.hpp"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <Windows.h>

namespace partake::daemon {

namespace internal {

auto add_lock_memory_privilege() -> bool {
    common::win32::win32_handle const h_token(
        [] {
            HANDLE h = INVALID_HANDLE_VALUE;
            if (OpenProcessToken(GetCurrentProcess(),
                                 TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES,
                                 &h) == 0) {
                auto err = GetLastError();
                auto msg = common::win32::strerror(err);
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
        auto msg = common::win32::strerror(err);
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
        auto msg = common::win32::strerror(err);
        spdlog::error("AdjustTokenPrivileges: {}: {} ({})",
                      SE_LOCK_MEMORY_NAME, msg, err);
        return false;
    }
    spdlog::info("AdjustTokenPrivileges: {}: success", SE_LOCK_MEMORY_NAME);
    return true;
}

auto create_autodeleted_file(std::filesystem::path const &path, bool force)
    -> common::win32::win32_handle {
    auto h_file = common::win32::win32_handle(
        CreateFileA(path.string().c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                    nullptr, force ? CREATE_ALWAYS : CREATE_NEW,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                    nullptr),
        spdlog::default_logger());
    if (not h_file.is_valid()) {
        auto err = GetLastError();
        auto msg = common::win32::strerror(err);
        spdlog::error("CreateFile: {}: {} ({})", path.string(), msg, err);
    } else {
        spdlog::info("CreateFile: {}: success, handle {}", path.string(),
                     h_file.get());
    }
    return h_file;
}

auto create_file_mapping(common::win32::win32_handle const &file_handle,
                         std::string const &name, std::size_t size,
                         bool use_large_pages) -> common::win32::win32_handle {
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
    auto h_mapping = raw_handle == nullptr
                         ? common::win32::win32_handle()
                         : common::win32::win32_handle(
                               raw_handle, spdlog::default_logger());
    // Return value does not indicate failure when the mapping already exists,
    // but GetLastError() does.
    auto err = GetLastError();
    if (not h_mapping.is_valid() || err == ERROR_ALREADY_EXISTS) {
        auto msg = common::win32::strerror(err);
        spdlog::error("CreateFileMapping: {}: {} ({})", name, msg, err);
        return {};
    }
    spdlog::info("CreateFileMapping: {}: success, handle {}", name,
                 h_mapping.get());
    return h_mapping;
}

win32_map_view::win32_map_view(common::win32::win32_handle const &h_mapping,
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
        auto msg = common::win32::strerror(err);
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
            auto msg = common::win32::strerror(err);
            spdlog::error("UnmapViewOfFile: addr {}: {} ({})", addr, msg, err);
        } else {
            spdlog::info("UnmapViewOfFile: addr {}: success", addr);
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
        {},
        internal::create_file_mapping({}, mapping_name, size, use_large_pages),
        size, use_large_pages);
}

auto create_win32_file_shmem(std::filesystem::path const &path,
                             std::string const &mapping_name, std::size_t size,
                             bool force, bool use_large_pages) -> win32_shmem {
    auto const granularity = use_large_pages ? large_page_minimum()
                                             : system_allocation_granularity();
    if (not round_up_or_check_size(size, granularity))
        return {};

    auto h_file = internal::create_autodeleted_file(path, force);
    if (not h_file.is_valid())
        return {};
    auto h_mapping = internal::create_file_mapping(h_file, mapping_name, size,
                                                   use_large_pages);
    return win32_shmem(std::move(h_file), std::move(h_mapping), size,
                       use_large_pages);
}

} // namespace partake::daemon

#endif // _WIN32
