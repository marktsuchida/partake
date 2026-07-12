/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "token.hpp"
#include "types.hpp"

#include <functional>
#include <memory>
#include <system_error>

namespace partake::client {

class connection;
class objview;
class event;

namespace internal {

struct event_payload; // Defined in src/event_impl.hpp.

auto make_event(std::shared_ptr<event_payload> payload) noexcept -> event;

} // namespace internal

// The completion of a submitted operation, pulled from a queue.
//
// Copyable and movable; copies SHARE the payload, so the continuation (see
// deliver()) is one-shot across all copies. Copies are not thread-safe with
// respect to each other (this matches the queue's single-drainer contract).
//
// An empty (default-constructed) event is benignly queryable: id() is 0,
// type() is op_type::none, error() is success, user_data() is nullptr,
// get_connection()/object() return empty handles, key() is invalid,
// zeroed() is false, and deliver() returns false.
class event {
  public:
    event() noexcept = default; // Empty; type() == op_type::none.

    [[nodiscard]] auto id() const noexcept -> op_id;
    [[nodiscard]] auto type() const noexcept -> op_type;
    [[nodiscard]] auto error() const noexcept -> std::error_code;
    [[nodiscard]] auto user_data() const noexcept -> void *;

    // Payload accessors; empty/default unless the op type provides them:
    [[nodiscard]] auto get_connection() const -> connection; // connect
    [[nodiscard]] auto object() const -> objview;       // alloc, open, share,
                                                        // unshare
    [[nodiscard]] auto key() const noexcept -> token;   // create_voucher (the
                                                        // voucher), unshare
                                                        // (the new key)
    [[nodiscard]] auto zeroed() const noexcept -> bool; // alloc, unshare

    // Invoke the continuation stored at submit time, if any, consuming it.
    // Returns false if there is no (remaining) continuation, such as when
    // the op was submitted with user_data instead.
    auto deliver() -> bool;

  private:
    friend auto internal::make_event(
        std::shared_ptr<internal::event_payload> payload) noexcept -> event;

    explicit event(std::shared_ptr<internal::event_payload> p) noexcept;

    std::shared_ptr<internal::event_payload> pl;
};

using completion = std::function<void(event &&)>;

} // namespace partake::client
