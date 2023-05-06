/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifndef _WIN32

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace partake::daemon::posix {

auto strerror(int errn) noexcept -> std::string;

// RAII for user-provided fd - close()
class file_descriptor {
  public:
    static constexpr int invalid_fd = -1;

  private:
    int fd = invalid_fd;

  public:
    file_descriptor() noexcept = default;

    explicit file_descriptor(int filedes) noexcept : fd(filedes) {}

    ~file_descriptor() { close(); }

    file_descriptor(file_descriptor &&other) noexcept
        : fd(std::exchange(other.fd, invalid_fd)) {}

    auto operator=(file_descriptor &&rhs) noexcept -> file_descriptor & {
        close();
        fd = std::exchange(rhs.fd, invalid_fd);
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return fd != invalid_fd;
    }

    [[nodiscard]] auto get() const noexcept -> int { return fd; }

    auto close() noexcept -> bool;
};

// RAII to ensure given file(-like) is unlinked
class unlinkable {
  public:
    using unlink_func = auto(*)(char const *) -> int;

  private:
    std::string nm;
    unlink_func unlink_fn = nullptr;
    std::string fn_name;

  public:
    unlinkable() noexcept = default;

    explicit unlinkable(std::string_view name) noexcept;

    explicit unlinkable(std::string_view name, unlink_func func,
                        std::string_view func_name) noexcept
        : nm(name), unlink_fn(func), fn_name(func_name) {
        assert(func != nullptr);
        assert(not func_name.empty());
    }

    ~unlinkable() { unlink(); }

    unlinkable(unlinkable &&other) noexcept
        : nm(std::exchange(other.nm, {})),
          unlink_fn(std::exchange(other.unlink_fn, nullptr)),
          fn_name(std::exchange(other.fn_name, {})) {}

    auto operator=(unlinkable &&rhs) noexcept -> unlinkable & {
        unlink();
        nm = std::exchange(rhs.nm, {});
        unlink_fn = std::exchange(rhs.unlink_fn, nullptr);
        fn_name = std::exchange(rhs.fn_name, {});
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return not nm.empty();
    }

    [[nodiscard]] auto name() const noexcept -> std::string { return nm; }

    auto unlink() noexcept -> bool {
        if (nm.empty())
            return true;
        bool ret = false;
        errno = 0;
        if (unlink_fn(nm.c_str())) {
            auto err = errno;
            auto msg = strerror(err);
            spdlog::error("{}: {}: {} ({})", fn_name, nm, msg, err);
        } else {
            spdlog::info("{}: {}: success", fn_name, nm);
            ret = true;
        }
        nm.clear();
        return ret;
    }
};

} // namespace partake::daemon::posix

#endif // _WIN32
