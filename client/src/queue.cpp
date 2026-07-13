/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/queue.hpp"

#include "partake/event.hpp"
#include "partake/types.hpp"
#include "queue_impl.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace partake::client {

queue::queue() : impl_(std::make_shared<internal::queue_impl>()) {}

queue::~queue() {
    if (impl_)
        impl_->close();
}

auto queue::operator=(queue &&other) noexcept -> queue & {
    if (this != &other) {
        if (impl_)
            impl_->close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

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
    // Deliver only the events present at entry: pushes during delivery
    // (from the I/O thread, or synchronous ones from submits on a stopped
    // client) re-signal the wakeup handle and are delivered by the next
    // call, so this terminates even if a continuation resubmits
    // unconditionally. Events stay queued until popped, so an exception
    // from a continuation (which propagates) cannot lose the ones not yet
    // delivered.
    auto const n = impl_->size();
    for (std::size_t i = 0; i < n; ++i) {
        auto ev = impl_->try_pop();
        if (not ev)
            break;
        if (not ev->deliver() and fallback)
            fallback(std::move(*ev));
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
