/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef _WIN32

#include "random.hpp"
#include "win32.hpp"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace partake::daemon {

namespace internal {

// RAII for MapViewOfFile() - UnmapViewOfFile()
class win32_map_view {
    void *addr = nullptr;
    std::size_t siz = 0;

  public:
    win32_map_view() noexcept = default;

    explicit win32_map_view(win32::win32_handle const &h_mapping,
                            std::size_t size,
                            bool use_large_pages = false) noexcept;

    ~win32_map_view() { unmap(); }

    win32_map_view(win32_map_view &&other) noexcept
        : addr(other.addr), siz(other.siz) {
        other.addr = nullptr;
        other.siz = 0;
    }

    auto operator=(win32_map_view &&rhs) noexcept -> win32_map_view & {
        unmap();
        addr = std::exchange(rhs.addr, nullptr);
        siz = std::exchange(rhs.siz, 0);
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return addr != nullptr;
    }

    [[nodiscard]] auto address() const noexcept -> void * { return addr; }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return siz; }

  private:
    void unmap() noexcept;
};

} // namespace internal

class win32_shmem {
    win32::win32_handle h_file;
    win32::win32_handle h_mapping;
    internal::win32_map_view view;

  public:
    win32_shmem() noexcept = default;

    explicit win32_shmem(win32::win32_handle &&file_handle,
                         win32::win32_handle &&mapping_handle,
                         std::size_t size, bool use_large_pages) noexcept
        : h_file(std::move(file_handle)), h_mapping(std::move(mapping_handle)),
          view(h_mapping, size, use_large_pages) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return h_mapping.is_valid() && view.is_valid();
    }

    [[nodiscard]] auto address() const noexcept -> void * {
        return view.address();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return view.size();
    }
};

inline auto generate_win32_file_mapping_name() noexcept -> std::string {
    return "Local\\partake-" + random_string(24);
}

auto create_win32_shmem(std::string const &mapping_name, std::size_t size,
                        bool use_large_pages = false) noexcept -> win32_shmem;

auto create_win32_file_shmem(std::filesystem::path const &path,
                             std::string const &mapping_name, std::size_t size,
                             bool force = false,
                             bool use_large_pages = false) noexcept
    -> win32_shmem;

} // namespace partake::daemon

#endif // _WIN32
