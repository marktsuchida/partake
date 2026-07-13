/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <memory>
#include <utility>

namespace partake::client::internal {

// One-slot, move-only, one-shot type-erased callable. Unlike std::function,
// it accepts move-only callables (such as asio completion handlers).
// Invocation consumes the stored callable, so the slot is empty during and
// after the call (re-entrant re-arming is safe).
template <typename Signature> class unique_handler;

template <typename R, typename... Args> class unique_handler<R(Args...)> {
    struct base {
        base() = default;
        virtual ~base() = default;
        base(base const &) = delete;
        auto operator=(base const &) -> base & = delete;
        base(base &&) = delete;
        auto operator=(base &&) -> base & = delete;
        virtual auto invoke(Args... args) -> R = 0;
    };

    template <typename F> struct impl final : base {
        F f;
        explicit impl(F func) : f(std::move(func)) {}
        auto invoke(Args... args) -> R override {
            return std::move(f)(std::forward<Args>(args)...);
        }
    };

    std::unique_ptr<base> fn;

  public:
    unique_handler() noexcept = default;

    template <typename F>
    explicit unique_handler(F f)
        : fn(std::make_unique<impl<F>>(std::move(f))) {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return fn != nullptr;
    }

    auto operator()(Args... args) -> R {
        assert(fn);
        auto f = std::move(fn);
        return f->invoke(std::forward<Args>(args)...);
    }
};

} // namespace partake::client::internal
