/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef _WIN32

#include <cstddef>
#include <utility>

namespace partake::daemon {

namespace internal {

// RAII for shmget() - shmctl(IPC_RMID)
class sysv_shmem_id {
    int shmid = -1;
    std::size_t siz = 0;

  public:
    sysv_shmem_id() noexcept = default;

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit sysv_shmem_id(int id, std::size_t size) : shmid(id), siz(size) {}

    ~sysv_shmem_id() { remove(); }

    sysv_shmem_id(sysv_shmem_id const &) = delete;
    auto operator=(sysv_shmem_id const &) = delete;

    sysv_shmem_id(sysv_shmem_id &&other) noexcept
        : shmid(std::exchange(other.shmid, -1)),
          siz(std::exchange(other.siz, 0)) {}

    auto operator=(sysv_shmem_id &&rhs) noexcept -> sysv_shmem_id & {
        remove();
        shmid = std::exchange(rhs.shmid, -1);
        siz = std::exchange(rhs.siz, 0);
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool { return shmid >= 0; }

    [[nodiscard]] auto id() const noexcept -> int { return shmid; }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return siz; }

    auto remove() -> bool;
};

// RAII for shmat() - shmdt()
class sysv_shmem_attachment {
    void *addr = nullptr;

  public:
    sysv_shmem_attachment() noexcept = default;

    // The shared memory may need to outlive the attachment on some systems.
    explicit sysv_shmem_attachment(int id);

    ~sysv_shmem_attachment() { detach(); }

    sysv_shmem_attachment(sysv_shmem_attachment const &) = delete;
    auto operator=(sysv_shmem_attachment const &) = delete;

    sysv_shmem_attachment(sysv_shmem_attachment &&other) noexcept
        : addr(std::exchange(other.addr, nullptr)) {}

    auto operator=(sysv_shmem_attachment &&rhs) noexcept
        -> sysv_shmem_attachment & {
        detach();
        addr = std::exchange(rhs.addr, nullptr);
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return addr != nullptr;
    }

    [[nodiscard]] auto address() const noexcept -> void * { return addr; }

    auto detach() -> bool;
};

} // namespace internal

class sysv_shmem {
    internal::sysv_shmem_id shmid;
    internal::sysv_shmem_attachment attachment;

  public:
    sysv_shmem() noexcept = default;

    explicit sysv_shmem(internal::sysv_shmem_id &&id)
        : shmid(std::move(id)), attachment(shmid.id()) {}

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return shmid.is_valid() && attachment.is_valid();
    }

    [[nodiscard]] auto id() const noexcept -> int { return shmid.id(); }

    [[nodiscard]] auto address() const noexcept -> void * {
        return attachment.address();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return shmid.size();
    }

    auto remove() -> bool { return shmid.remove(); }

    auto detach() -> bool { return attachment.detach(); }
};

auto create_sysv_shmem(std::size_t size, bool use_huge_pages = false,
                       std::size_t huge_page_size = 0) -> sysv_shmem;

auto create_sysv_shmem(int key, std::size_t size, bool force = false,
                       bool use_huge_pages = false,
                       std::size_t huge_page_size = 0) -> sysv_shmem;

} // namespace partake::daemon

#endif // _WIN32
