/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_mmap.hpp"

#ifndef _WIN32

#include "page_size.hpp"
#include "random.hpp"
#include "sizes.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/posix_shm.h>
#endif

namespace partake::daemon {

namespace internal {

auto create_posix_shmem(std::string const &name, bool force)
    -> std::pair<common::posix::unlinkable, common::posix::file_descriptor> {
#ifdef __APPLE__
    if (force) {
        // On macOS, ftruncate() only succeeds once on a POSIX shmem, so we
        // need to unlink before reusing a name (even with shm_open(O_CREAT)
        // below).
        errno = 0;
        if (::shm_unlink(name.c_str()) != 0) {
            auto err = errno;
            if (err != ENOENT) {
                auto msg = common::posix::strerror(err);
                spdlog::error("shm_unlink: {}: {} ({})", name, msg, err);
            }
        }
    }
#endif

    errno = 0;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
    auto fd = common::posix::file_descriptor(
        ::shm_open(name.c_str(), O_RDWR | O_CREAT | (force ? 0 : O_EXCL),
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),
        spdlog::default_logger());
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    if (not fd.is_valid()) {
        int err = errno;
        auto msg = common::posix::strerror(err);
        spdlog::error("shm_open: {}: {} ({})", name, msg, err);
        return {};
    }
    spdlog::info("shm_open: {}: success; fd {}", name, fd.get());
    return {common::posix::unlinkable(name, ::shm_unlink, "shm_unlink",
                                      spdlog::default_logger()),
            std::move(fd)};
}

auto create_regular_file(std::string const &path, bool force)
    -> std::pair<common::posix::unlinkable, common::posix::file_descriptor> {
    errno = 0;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
    auto fd = common::posix::file_descriptor(
        ::open(path.c_str(),
               O_RDWR | O_CREAT | (force ? 0 : O_EXCL) | O_CLOEXEC,
               S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),
        spdlog::default_logger());
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    if (not fd.is_valid()) {
        int err = errno;
        auto msg = common::posix::strerror(err);
        spdlog::error("open: {}: {} ({})", path, msg, err);
        return {};
    }
    spdlog::info("open: {}: success; fd {}", path, fd.get());
    return {common::posix::unlinkable(path, spdlog::default_logger()),
            std::move(fd)};
}

mmap_mapping::mmap_mapping(std::size_t size,
                           common::posix::file_descriptor const &fd) {
    if (not fd.is_valid())
        return;

    errno = 0;
    if (::ftruncate(fd.get(), static_cast<off_t>(size)) != 0) {
        int err = errno;
        auto msg = common::posix::strerror(err);
        spdlog::error("ftruncate: fd {}, size {}: {} ({})", fd.get(), size,
                      msg, err);
        return;
    }
    spdlog::info("ftruncate: fd {}, size {}: success", fd.get(), size);

    if (size > 0) {
        errno = 0;
        void *a = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                         fd.get(), 0);
        if (a == MAP_FAILED) { // NOLINT(performance-no-int-to-ptr)
            int err = errno;
            auto msg = common::posix::strerror(err);
            spdlog::error("mmap: fd {}, size {}: {} ({})", fd.get(), size, msg,
                          err);
        } else {
            spdlog::info("mmap: fd {}, size {}: success; addr {}", fd.get(),
                         size, a);
            addr = a;
            siz = size;
        }
    }
}

auto mmap_mapping::unmap() -> bool {
    if (addr == nullptr)
        return true;
    bool ret = false;
    errno = 0;
    if (::munmap(addr, siz) != 0) {
        int err = errno;
        auto msg = common::posix::strerror(err);
        spdlog::error("munmap: addr {}: {} ({})", addr, msg, err);
    } else {
        spdlog::info("munmap: addr {}: success", addr);
        ret = true;
    }
    siz = 0;
    addr = nullptr;
    return ret;
}

auto generate_posix_shmem_name() -> std::string {
    // Max: macOS 31, Linux 255, FreeBSD 1023.
    static constexpr std::size_t name_len = 31;
    std::string name = "/partake-";
    name += common::random_string(name_len - name.size()); // 22 random chars
    return name;
}

auto generate_filename() -> std::string {
    auto const filename = "partake-" + common::random_string(24);
#ifdef __APPLE__
    // Avoid the long, messy path returned by temp_directory_path().
    // Assume no macOS system is without /tmp.
    auto p = std::filesystem::path("/tmp");
#else
    auto p = std::filesystem::temp_directory_path();
#endif
    return p / filename;
}

} // namespace internal

auto create_posix_mmap_shmem(std::string const &name, std::size_t size,
                             bool force) -> mmap_shmem {
    auto [shmem, fd] = internal::create_posix_shmem(name, force);
    auto psize = page_size();
    if (not round_up_or_check_size(size, psize))
        return {};
    return mmap_shmem(std::move(shmem), fd, size);
}

auto create_posix_mmap_shmem(std::size_t size) -> mmap_shmem {
    auto name = internal::generate_posix_shmem_name();
    return create_posix_mmap_shmem(name, size, false);
}

auto create_file_mmap_shmem(std::string const &name, std::size_t size,
                            bool force) -> mmap_shmem {
    auto [file, fd] = internal::create_regular_file(name, force);
#ifdef __linux__
    auto psize = file_page_size(fd.get());
#else
    auto psize = page_size();
#endif
    if (not round_up_or_check_size(size, psize))
        return {};
    return mmap_shmem(std::move(file), fd, size);
}

auto create_file_mmap_shmem(std::size_t size) -> mmap_shmem {
    auto name = internal::generate_filename();
    return create_file_mmap_shmem(name, size, false);
}

} // namespace partake::daemon

#endif // _WIN32
