/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "shmem_attach.hpp" // posix_mmap_region, sysv_attach_region

#include <cstddef>
#include <cstdint>
#include <variant>

namespace partake::client::internal {

// One shared, whole-segment mapping. Immovable: only ever held through
// shared_ptr, for pointer stability.
class mapping {
    void *base_ = nullptr;
    std::size_t size_ = 0;
    std::variant<posix_mmap_region, sysv_attach_region> backing_;

  public:
    mapping(void *base, std::size_t size,
            std::variant<posix_mmap_region, sysv_attach_region> backing)
        : base_(base), size_(size), backing_(std::move(backing)) {}

    ~mapping() = default;
    mapping(mapping const &) = delete;
    auto operator=(mapping const &) -> mapping & = delete;
    mapping(mapping &&) = delete;
    auto operator=(mapping &&) -> mapping & = delete;

    [[nodiscard]] auto base() const noexcept -> void * { return base_; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }
};

class mapping_policy {
  public:
    mapping_policy() = default;
    virtual ~mapping_policy() = default;
    mapping_policy(mapping_policy const &) = default;
    auto operator=(mapping_policy const &) -> mapping_policy & = default;
    mapping_policy(mapping_policy &&) = default;
    auto operator=(mapping_policy &&) -> mapping_policy & = default;

    [[nodiscard]] virtual auto object_data(mapping const &m,
                                           std::uint64_t offset) const noexcept
        -> void * = 0;
};

// Performance mode uses a single mapping for the entire segment, with no
// mprotect() to prevent illegal access. Other mapping types may be added in
// the future.
class performance_mapping_policy final : public mapping_policy {
  public:
    [[nodiscard]] auto object_data(mapping const &m,
                                   std::uint64_t offset) const noexcept
        -> void * override {
        return static_cast<char *>(m.base()) + offset;
    }
};

} // namespace partake::client::internal
