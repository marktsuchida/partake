/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "requests.hpp" // segment_spec

#include <tl/expected.hpp>

#include <cstddef>
#include <memory>
#include <system_error>
#include <utility>

namespace partake::client::internal {

class mapping;

// Attach-only RAII wrappers around a shared-memory region.

// mirrors daemon::internal::mmap_mapping (attach half): ::munmap on dtor.
class posix_mmap_region {
    void *addr_ = nullptr;
    std::size_t size_ = 0;

  public:
    posix_mmap_region() noexcept = default;
    posix_mmap_region(void *addr, std::size_t size) noexcept
        : addr_(addr), size_(size) {}
    ~posix_mmap_region();

    posix_mmap_region(posix_mmap_region const &) = delete;
    auto operator=(posix_mmap_region const &) -> posix_mmap_region & = delete;

    posix_mmap_region(posix_mmap_region &&other) noexcept
        : addr_(std::exchange(other.addr_, nullptr)),
          size_(std::exchange(other.size_, 0)) {}
    auto operator=(posix_mmap_region &&rhs) noexcept -> posix_mmap_region &;
};

// mirrors daemon::internal::sysv_shmem_attachment: ::shmdt on dtor.
class sysv_attach_region {
    void *addr_ = nullptr;
    std::size_t size_ = 0;

  public:
    sysv_attach_region() noexcept = default;
    sysv_attach_region(void *addr, std::size_t size) noexcept
        : addr_(addr), size_(size) {}
    ~sysv_attach_region();

    sysv_attach_region(sysv_attach_region const &) = delete;
    auto operator=(sysv_attach_region const &)
        -> sysv_attach_region & = delete;

    sysv_attach_region(sysv_attach_region &&other) noexcept
        : addr_(std::exchange(other.addr_, nullptr)),
          size_(std::exchange(other.size_, 0)) {}
    auto operator=(sysv_attach_region &&rhs) noexcept -> sysv_attach_region &;
};

// Attach to the whole shared-memory segment described by spec and return a
// shared mapping (always maps read-write for now). Syscall failures are
// returned as std::error_code(errno, std::system_category()); an unsupported
// spec returns client_errc::protocol_violation.
auto attach(segment_spec const &spec)
    -> tl::expected<std::shared_ptr<mapping>, std::error_code>;

} // namespace partake::client::internal
