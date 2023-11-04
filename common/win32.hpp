/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef _WIN32

#include "logging.hpp"

#include <spdlog/spdlog.h>

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// Avoid including Windows.h in this header
using HANDLE = void *;

namespace partake::common::win32 {

auto strerror(unsigned err) -> std::string;

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
    std::shared_ptr<spdlog::logger> lgr;

  public:
    win32_handle() noexcept = default;

    explicit win32_handle(HANDLE h,
                          std::shared_ptr<spdlog::logger> logger = {})
        : h(h), lgr(logger ? std::move(logger) : null_logger()) {
        assert(lgr);
    }

    ~win32_handle() { close(); }

    win32_handle(win32_handle &&other) noexcept
        : h(std::exchange(other.h, invalid_handle())),
          lgr(std::exchange(other.lgr, {})) {}

    auto operator=(win32_handle &&rhs) noexcept -> win32_handle & {
        close();
        h = std::exchange(rhs.h, invalid_handle());
        lgr = std::exchange(rhs.lgr, {});
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return h != invalid_handle();
    }

    [[nodiscard]] auto get() const noexcept -> HANDLE { return h; }

    auto close() -> bool;
};

class unlinkable {
  public:
    using unlink_func = auto (*)(char const *) -> int; // (*)(LPCSTR) -> BOOL

  private:
    std::string nm;
    unlink_func unlink_fn = nullptr;
    std::string fn_name;
    std::shared_ptr<spdlog::logger> lgr;

  public:
    unlinkable() noexcept = default;

    explicit unlinkable(std::string_view name,
                        std::shared_ptr<spdlog::logger> logger = {});

    explicit unlinkable(std::string_view name, unlink_func func,
                        std::string_view func_name,
                        std::shared_ptr<spdlog::logger> logger = {})
        : nm(name), unlink_fn(func), fn_name(func_name),
          lgr(logger ? std::move(logger) : null_logger()) {
        assert(func != nullptr);
        assert(not func_name.empty());
        assert(lgr);
    }

    ~unlinkable() { unlink(); }

    unlinkable(unlinkable &&other) noexcept
        : nm(std::exchange(other.nm, {})),
          unlink_fn(std::exchange(other.unlink_fn, nullptr)),
          fn_name(std::exchange(other.fn_name, {})),
          lgr(std::exchange(other.lgr, {})) {}

    auto operator=(unlinkable &&rhs) noexcept -> unlinkable & {
        unlink();
        nm = std::exchange(rhs.nm, {});
        unlink_fn = std::exchange(rhs.unlink_fn, nullptr);
        fn_name = std::exchange(rhs.fn_name, {});
        lgr = std::exchange(rhs.lgr, {});
        return *this;
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return not nm.empty();
    }

    [[nodiscard]] auto name() const -> std::string { return nm; }

    auto unlink() -> bool;
};

} // namespace partake::common::win32

#endif // _WIN32
