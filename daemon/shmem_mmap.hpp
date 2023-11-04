/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef _WIN32

#include "posix.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace partake::daemon {

namespace internal {

class mmap_mapping {
    std::size_t siz = 0;
    void *addr = nullptr;

  public:
    mmap_mapping() noexcept = default;

    explicit mmap_mapping(std::size_t size,
                          common::posix::file_descriptor const &fd);

    ~mmap_mapping() { unmap(); }

    mmap_mapping(mmap_mapping &&other) noexcept
        : siz(other.siz), addr(other.addr) {
        other = {};
    }

    auto operator=(mmap_mapping &&rhs) noexcept -> mmap_mapping & {
        unmap();
        siz = std::exchange(rhs.siz, 0);
        addr = std::exchange(rhs.addr, nullptr);
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return addr != nullptr;
    }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return siz; }
    [[nodiscard]] auto address() const noexcept -> void * { return addr; }

    auto unmap() -> bool;
};

} // namespace internal

class mmap_shmem {
    common::posix::unlinkable ent;
    internal::mmap_mapping mapping;

  public:
    mmap_shmem() noexcept = default;

    explicit mmap_shmem(common::posix::unlinkable &&entry,
                        common::posix::file_descriptor const &fd,
                        std::size_t size)
        : ent(std::move(entry)), mapping(size, fd) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return mapping.is_valid();
    }

    [[nodiscard]] auto name() const -> std::string { return ent.name(); }

    [[nodiscard]] auto address() const noexcept -> void * {
        return mapping.address();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return mapping.size();
    }

    auto unlink() -> bool { return ent.unlink(); }

    auto unmap() -> bool { return mapping.unmap(); }
};

auto create_posix_mmap_shmem(std::string const &name, std::size_t size,
                             bool force) -> mmap_shmem;

auto create_posix_mmap_shmem(std::size_t size) -> mmap_shmem;

auto create_file_mmap_shmem(std::string const &filename, std::size_t size,
                            bool force) -> mmap_shmem;

auto create_file_mmap_shmem(std::size_t size) -> mmap_shmem;

} // namespace partake::daemon

#endif // _WIN32
