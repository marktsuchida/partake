/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event.hpp"
#include "types.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>

namespace partake::client {

class queue;

namespace internal {

class queue_impl;

auto get_queue_impl(queue const &q) noexcept
    -> std::shared_ptr<queue_impl> const &;

} // namespace internal

// Completion queue: operation completions are pushed by the library and
// pulled by the application, which can park on the wakeup handle.
//
// Threading: any single thread at a time may drain/dispatch/wait (external
// serialization by the caller); submits (pushes) may race freely with
// draining.
//
// The wakeup handle belongs to the shared internal state, which stays alive
// while anything references it (later: connections bound to this queue), so
// it remains valid across moves of this handle object. Calling any method
// on a moved-from queue is a programmer error (terminates).
class queue {
  public:
    // Creates the wakeup pipe; throws std::system_error on failure.
    queue();
    ~queue();

    queue(queue const &) = delete;
    auto operator=(queue const &) -> queue & = delete;
    queue(queue &&) noexcept = default;
    auto operator=(queue &&) noexcept -> queue & = default;

    // Poll/select-able handle that becomes readable when the queue is
    // nonempty. Do not read from or close it; use drain()/wait_one().
    [[nodiscard]] auto wakeup() const noexcept -> wakeup_handle;

    // Nonblocking bulk retrieval of up to max_events events into out;
    // returns the number retrieved.
    auto drain(event *out, std::size_t max_events) -> std::size_t;

    // Blocking retrieval of a single event; negative timeout = infinite.
    auto wait_one(std::chrono::milliseconds timeout) -> std::optional<event>;

    // Drain everything, deliver() each event; events with no continuation
    // go to fallback (dropped if fallback is empty).
    void dispatch(completion const &fallback = {});

  private:
    friend auto internal::get_queue_impl(queue const &q) noexcept
        -> std::shared_ptr<internal::queue_impl> const &;

    std::shared_ptr<internal::queue_impl> impl_;
};

} // namespace partake::client
