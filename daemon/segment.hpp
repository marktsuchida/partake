/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace partake::daemon {

// Segment spec is the info needed by clients; segment config is the info
// needed by the daemon to create the segment. Their fields partially overlap.

struct posix_mmap_segment_spec {
    std::string name; // shm_open() name; non-empty
};

struct file_mmap_segment_spec {
    std::string filename; // Canonicalized; non-empty
};

struct sysv_segment_spec {
    std::int32_t shm_id = -1; // >= 0 for valid spec
};

struct win32_segment_spec {
    std::string name; // Named file mapping name; non-empty
    bool use_large_pages = false;
};

struct segment_spec {
    std::variant<posix_mmap_segment_spec, file_mmap_segment_spec,
                 sysv_segment_spec, win32_segment_spec>
        spec;
    std::size_t size = 0;
};

struct posix_mmap_segment_config {
    std::string name;   // shm_open() name; generate if empty
    bool force = false; // Replace existing shm_open() name
};

struct file_mmap_segment_config {
    std::string filename; // Generate tempfile if empty
    bool force = false;   // Replace existing file
};

struct sysv_segment_config {
    std::int32_t key = 0; // Auto-select if zero
    bool force = false;   // Replace existing key
    bool use_huge_pages =
        false; // Linux (TODO: Support choice of huge page size)
};

struct win32_segment_config {
    std::string filename; // Use system page file if empty
    std::string name;     // Named file mapping name; generate if empty
    bool force = false;   // Replace existing filename and/or mapping name
    bool use_large_pages = false; // Requires empty filename
};

struct segment_config {
    std::variant<posix_mmap_segment_config, file_mmap_segment_config,
                 sysv_segment_config, win32_segment_config>
        method;
    std::size_t size = 0;
};

namespace internal {

struct segment_impl {
    virtual ~segment_impl() = default;
    // No move or copy (used with unique_ptr)
    auto operator=(segment_impl &&) = delete;

    [[nodiscard]] virtual auto is_valid() const noexcept -> bool = 0;
    [[nodiscard]] virtual auto size() const noexcept -> std::size_t = 0;
    [[nodiscard]] virtual auto spec() const noexcept -> segment_spec = 0;
};

} // namespace internal

class segment {
    using impl_ptr = std::unique_ptr<internal::segment_impl>;
    impl_ptr impl;

  public:
    segment() noexcept;

    explicit segment(segment_config const &config) noexcept;

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return impl->is_valid();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return impl->size();
    }

    [[nodiscard]] auto spec() const noexcept -> segment_spec {
        return impl->spec();
    }
};

} // namespace partake::daemon
