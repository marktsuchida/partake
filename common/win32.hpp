/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef _WIN32

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

// Avoid including Windows.h in this header
using HANDLE = void *;

namespace partake::common::win32 {

auto strerror(unsigned err) noexcept -> std::string;

// RAII for user-provided HANDLE - CloseHandle()
class win32_handle {
  public:
    // Win32 INVALID_HANDLE_VALUE (Use function because we can't make
    // const(expr) with a reinterpret_cast value.)
    static auto invalid_handle() noexcept -> HANDLE {
        return reinterpret_cast<HANDLE>(-1); // NOLINT
    }

  private:
    HANDLE h = invalid_handle();

  public:
    win32_handle() noexcept = default;

    explicit win32_handle(HANDLE h) noexcept : h(h) {}

    ~win32_handle() { close(); }

    win32_handle(win32_handle &&other) noexcept
        : h(std::exchange(other.h, invalid_handle())) {}

    auto operator=(win32_handle &&rhs) noexcept -> win32_handle & {
        close();
        h = std::exchange(rhs.h, invalid_handle());
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return h != invalid_handle();
    }

    [[nodiscard]] auto get() const noexcept -> HANDLE { return h; }

    auto close() noexcept -> bool;
};

class unlinkable {
  public:
    using unlink_func = auto (*)(char const *) -> int; // (*)(LPCSTR) -> BOOL

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

    auto unlink() noexcept -> bool;
};

} // namespace partake::common::win32

#endif // _WIN32
