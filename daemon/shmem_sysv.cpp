/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_sysv.hpp"

#ifndef _WIN32

#include "page_size.hpp"
#include "posix.hpp"
#include "sizes.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <type_traits>

#include <sys/shm.h>
#include <sys/stat.h>

// SHM_HUGE_SHIFT is defined in linux/shm.h but that header appears to conflict
// with sys/shm.h. It is safe to hard-code this value because it is part of the
// stable kernel ABI (ultimately it is defined in asm-generic/hugetlb_encode.h
// as HUGETLB_FLAG_ENCODE_SHIFT).
#if defined(__linux__) && not defined(SHM_HUGE_SHIFT)
#define SHM_HUGE_SHIFT 26 // NOLINT(cppcoreguidelines-macro-usage)
#endif

namespace partake::daemon {

// We use 'int' in our interface to avoid including Unix headers in our header.
static_assert(std::is_same_v<int, key_t>);

// We assume IPC_PRIVATE equals 0 and use 0 as request to auto-select the key.
static_assert(IPC_PRIVATE == 0);

namespace internal {

namespace {

// The check sees through the noexcept lambda to ::umask(), which, like any C
// function, is not declared noexcept (but cannot throw).
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
::mode_t const the_umask = []() noexcept {
    // Store the umask at static initialization time so that we don't have any
    // chance of a data race.
    auto ret = ::umask(S_IRWXG | S_IRWXO);
    (void)::umask(ret); // Restore
    return ret;
}();

#ifdef __linux__
// Returns 0 if invalid size requested.
auto linux_page_size(bool use_huge_pages, std::size_t huge_page_size)
    -> std::size_t {
    if (use_huge_pages) {
        if (huge_page_size == 0)
            return default_huge_page_size();
        auto const sizes = huge_page_sizes();
        if (std::find(sizes.begin(), sizes.end(), huge_page_size) ==
            sizes.end())
            return 0;
        return huge_page_size;
    }
    return page_size();
}
#endif

} // namespace

auto create_sysv_shmem_id(int key, std::size_t size, bool force,
                          bool use_huge_pages, std::size_t huge_page_size)
    -> sysv_shmem_id {
    if (size == 0)
        return {};

    if (force && key != IPC_PRIVATE) {
        // We cannot reuse an existing key if the size grows (EEXIST; EINVAL on
        // macOS), so remove it first.
        errno = 0;
        int id = ::shmget(key, 0, 0);
        if (id < 0) {
            auto e = errno;
            if (e != ENOENT) {
                auto msg = common::posix::strerror(e);
                spdlog::error("shmget: key {}: {} ({})", key, msg, e);
            }
        } else {
            errno = 0;
            if (::shmctl(id, IPC_RMID, nullptr) != 0) {
                auto e = errno;
                if (e != ENOENT) {
                    auto msg = common::posix::strerror(e);
                    spdlog::error("shmctl IPC_RMID: id {}: {} ({})", id, msg,
                                  e);
                }
            }
        }
    }

#ifdef __linux__
    auto const psize = linux_page_size(use_huge_pages, huge_page_size);
    if (psize == 0) {
        spdlog::error("{} is not a supported huge page size",
                      human_readable_size(huge_page_size));
        return {};
    }
#else
    if (use_huge_pages) {
        spdlog::error("shmget: huge pages not supported on this platform");
        return {};
    }
    (void)huge_page_size;
    auto const psize = page_size();
#endif

    if (not round_up_or_check_size(size, psize))
        return {};

#ifdef __linux__
    int const huge_pages_flags = [use_huge_pages, huge_page_size]() {
        if (use_huge_pages) {
            int ret = SHM_HUGETLB;
            if (huge_page_size > 0)
                ret |= (static_cast<int>(log2_size(huge_page_size))
                        << SHM_HUGE_SHIFT);
            return ret;
        }
        return 0;
    }();
#else
    int const huge_pages_flags = 0;
#endif

    errno = 0;
    // shmget() does not use the umask, but we apply it ourselves to match the
    // behavior of files and POSIX shared memory. The executable bits do
    // nothing, so leave cleared.
    auto const perms = 0666 & ~the_umask;
    int id = ::shmget(key, size,
                      IPC_CREAT | (force ? 0 : IPC_EXCL) | huge_pages_flags |
                          perms);
    if (id < 0) {
        auto err = errno;
        auto msg = common::posix::strerror(err);
        spdlog::error("shmget: key {}: {} ({})", key, msg, err);
        return {};
    }

    spdlog::info("shmget: key {}: success; id {}", key, id);
    return sysv_shmem_id(id, size);
}

auto sysv_shmem_id::remove() -> bool {
    if (shmid < 0)
        return true;
    bool ret = false;
    errno = 0;
    if (::shmctl(shmid, IPC_RMID, nullptr) != 0) {
        int e = errno;
        auto msg = common::posix::strerror(e);
        spdlog::error("shmctl IPC_RMID: id {}: {} ({})", shmid, msg, e);
    } else {
        spdlog::info("shmctl IPC_RMID: id {}: success", shmid);
        ret = true;
    }
    shmid = -1;
    return ret;
}

sysv_shmem_attachment::sysv_shmem_attachment(int id) {
    if (id < 0)
        return;
    errno = 0;
    void *a = ::shmat(id, nullptr, 0);
    if (a == (void *)-1) { // NOLINT
        auto err = errno;
        auto msg = common::posix::strerror(err);
        spdlog::error("shmat: id {}: {} ({})", id, msg, err);
    } else {
        spdlog::info("shmat: id {}: success; addr {}", id, a);
        addr = a;
    }
}

auto sysv_shmem_attachment::detach() -> bool {
    if (addr == nullptr)
        return true;
    bool ret = false;
    errno = 0;
    if (::shmdt(addr) != 0) {
        int e = errno;
        auto msg = common::posix::strerror(e);
        spdlog::error("shmdt: addr {}: {} ({})", addr, msg, e);
    } else {
        spdlog::info("shmdt: addr {}: success", addr);
        ret = true;
    }
    addr = nullptr;
    return ret;
}

} // namespace internal

auto create_sysv_shmem(std::size_t size, bool use_huge_pages,
                       std::size_t huge_page_size) -> sysv_shmem {
    return sysv_shmem(internal::create_sysv_shmem_id(
        IPC_PRIVATE, size, false, use_huge_pages, huge_page_size));
}

auto create_sysv_shmem(int key, std::size_t size, bool force,
                       bool use_huge_pages, std::size_t huge_page_size)
    -> sysv_shmem {
    return sysv_shmem(internal::create_sysv_shmem_id(
        key, size, force, use_huge_pages, huge_page_size));
}

} // namespace partake::daemon

#endif // _WIN32
