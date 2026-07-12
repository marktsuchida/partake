/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/queue.hpp"

#include "partake/event.hpp"
#include "partake/types.hpp"
#include "queue_impl.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace partake::client {

queue::queue() : impl_(std::make_shared<internal::queue_impl>()) {}

queue::~queue() = default;

auto queue::wakeup() const noexcept -> wakeup_handle {
    if (not impl_) {
        assert(false);
        std::terminate();
    }
    return impl_->wakeup();
}

auto queue::drain(event *out, std::size_t max_events) -> std::size_t {
    if (not impl_) {
        assert(false);
        std::terminate();
    }
    return impl_->drain(out, max_events);
}

auto queue::wait_one(std::chrono::milliseconds timeout)
    -> std::optional<event> {
    if (not impl_) {
        assert(false);
        std::terminate();
    }
    return impl_->wait_one(timeout);
}

void queue::dispatch(completion const &fallback) {
    if (not impl_) {
        assert(false);
        std::terminate();
    }
    // This loop terminates because continuations cannot push synchronously:
    // pushes come from the I/O thread.
    std::array<event, 16> batch;
    for (;;) {
        auto const n = impl_->drain(batch.data(), batch.size());
        if (n == 0)
            return;
        for (std::size_t i = 0; i < n; ++i) {
            auto &ev = batch[i];
            if (not ev.deliver() and fallback)
                fallback(std::move(ev));
            ev = {};
        }
    }
}

namespace internal {

auto get_queue_impl(queue const &q) noexcept
    -> std::shared_ptr<queue_impl> const & {
    // Null for moved-from queues; callers must check.
    return q.impl_;
}

} // namespace internal

} // namespace partake::client
