/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_attach.hpp"

#include "mapping.hpp"

#include "partake/errors.hpp"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <utility>
#include <variant>

#ifndef _WIN32
#include "overloaded.hpp"
#include "posix.hpp"

#include <cerrno>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <unistd.h>
#endif

namespace partake::client::internal {

#ifndef _WIN32

namespace {

void unmap_region(void *addr, std::size_t size) {
    if (addr == nullptr)
        return;
    errno = 0;
    if (::munmap(addr, size) != 0) {
        int const err = errno;
        spdlog::error("munmap: addr {}: {} ({})", addr,
                      common::posix::strerror(err), err);
    }
}

void detach_region(void *addr) {
    if (addr == nullptr)
        return;
    errno = 0;
    if (::shmdt(addr) != 0) {
        int const err = errno;
        spdlog::error("shmdt: addr {}: {} ({})", addr,
                      common::posix::strerror(err), err);
    }
}

} // namespace

posix_mmap_region::~posix_mmap_region() { unmap_region(addr_, size_); }

auto posix_mmap_region::operator=(posix_mmap_region &&rhs) noexcept
    -> posix_mmap_region & {
    if (this != &rhs) {
        unmap_region(addr_, size_);
        addr_ = std::exchange(rhs.addr_, nullptr);
        size_ = std::exchange(rhs.size_, 0);
    }
    return *this;
}

sysv_attach_region::~sysv_attach_region() { detach_region(addr_); }

auto sysv_attach_region::operator=(sysv_attach_region &&rhs) noexcept
    -> sysv_attach_region & {
    if (this != &rhs) {
        detach_region(addr_);
        addr_ = std::exchange(rhs.addr_, nullptr);
        size_ = std::exchange(rhs.size_, 0);
    }
    return *this;
}

namespace {

auto attach_posix(posix_mmap_spec const &s, std::uint64_t size)
    -> tl::expected<std::shared_ptr<mapping>, std::error_code> {
    errno = 0;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
    auto fd = common::posix::file_descriptor(
        s.use_shm_open ? ::shm_open(s.name.c_str(), O_RDWR)
                       : ::open(s.name.c_str(), O_RDWR),
        spdlog::default_logger());
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    if (not fd.is_valid()) {
        int const err = errno;
        spdlog::error("{}: {}: {} ({})", s.use_shm_open ? "shm_open" : "open",
                      s.name, common::posix::strerror(err), err);
        return tl::unexpected(std::error_code(err, std::system_category()));
    }

    auto const map_size = static_cast<std::size_t>(size);
    errno = 0;
    void *addr = ::mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        fd.get(), 0);
    if (addr == MAP_FAILED) { // NOLINT(performance-no-int-to-ptr)
        int const err = errno;
        spdlog::error("mmap: {}: {} ({})", s.name,
                      common::posix::strerror(err), err);
        return tl::unexpected(std::error_code(err, std::system_category()));
    }
    // fd is closed here (its scope ends); the mapping outlives it.
    return std::make_shared<mapping>(addr, map_size,
                                     posix_mmap_region(addr, map_size));
}

auto attach_sysv(sysv_shmem_spec const &s, std::uint64_t size)
    -> tl::expected<std::shared_ptr<mapping>, std::error_code> {
    errno = 0;
    void *addr = ::shmat(s.shm_id, nullptr, 0);
    if (addr == (void *)-1) { // NOLINT
        int const err = errno;
        spdlog::error("shmat: id {}: {} ({})", s.shm_id,
                      common::posix::strerror(err), err);
        return tl::unexpected(std::error_code(err, std::system_category()));
    }
    auto const map_size = static_cast<std::size_t>(size);
    return std::make_shared<mapping>(addr, map_size,
                                     sysv_attach_region(addr, map_size));
}

} // namespace

auto attach(segment_spec const &spec)
    -> tl::expected<std::shared_ptr<mapping>, std::error_code> {
    return std::visit(
        common::overloaded{
            [&](posix_mmap_spec const &s) {
                return attach_posix(s, spec.size);
            },
            [&](sysv_shmem_spec const &s) {
                return attach_sysv(s, spec.size);
            },
            [&](win32_mapping_spec const &)
                -> tl::expected<std::shared_ptr<mapping>, std::error_code> {
                // Win32 attach is deferred to Stage 5.
                return tl::unexpected(
                    make_error_code(client_errc::protocol_violation));
            },
        },
        spec.spec);
}

#else // _WIN32

posix_mmap_region::~posix_mmap_region() = default;

auto posix_mmap_region::operator=(posix_mmap_region &&rhs) noexcept
    -> posix_mmap_region & {
    addr_ = std::exchange(rhs.addr_, nullptr);
    size_ = std::exchange(rhs.size_, 0);
    return *this;
}

sysv_attach_region::~sysv_attach_region() = default;

auto sysv_attach_region::operator=(sysv_attach_region &&rhs) noexcept
    -> sysv_attach_region & {
    addr_ = std::exchange(rhs.addr_, nullptr);
    size_ = std::exchange(rhs.size_, 0);
    return *this;
}

auto attach(segment_spec const &)
    -> tl::expected<std::shared_ptr<mapping>, std::error_code> {
    // POSIX/SysV attach unavailable; Win32 not yet implemented
    return tl::unexpected(make_error_code(client_errc::protocol_violation));
}

#endif // _WIN32

} // namespace partake::client::internal
