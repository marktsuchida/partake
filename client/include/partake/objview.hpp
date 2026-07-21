/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event.hpp"
#include "token.hpp"
#include "types.hpp"

#include <cstdint>
#include <memory>

namespace partake::client {

class objview;

namespace internal {

class objview_impl;

auto make_objview(std::shared_ptr<objview_impl> impl) noexcept -> objview;

auto get_objview_impl(objview const &ov) noexcept
    -> std::shared_ptr<objview_impl> const &;

} // namespace internal

// A view of a shared object, obtained from an alloc/open/share/unshare
// completion event.
//
// Copyable handle (shared impl). When the last handle to an OPEN objview is
// destroyed, a protocol Close is auto-submitted (fire-and-forget).
//
// An empty (default-constructed) objview answers only operator bool(); any
// other use -- even the const accessors -- is a programmer error
// (terminates).
class objview {
  public:
    objview() noexcept = default; // Empty.

    [[nodiscard]] explicit operator bool() const noexcept;

    [[nodiscard]] auto key() const noexcept -> token;
    [[nodiscard]] auto size() const noexcept -> std::uint64_t;
    [[nodiscard]] auto writable() const noexcept -> bool;

    // nullptr once closed; stable for the objview's open lifetime.
    [[nodiscard]] auto data() const noexcept -> void *;

    // Each submit returns an op_id immediately and has (..., void
    // *user_data) and (..., completion) overloads; the completion event is
    // delivered to the owning connection's queue.

    // This objview -> closed state.
    auto close(void *user_data) -> op_id;
    auto close(completion c) -> op_id;

    // default_-policy, written object -> immutable/shareable.
    // Event: object() = new read-only objview; this objview -> closed.
    auto share(void *user_data) -> op_id;
    auto share(completion c) -> op_id;

    // Event: object() = new writable objview with NEW key (event::key()),
    // zeroed(); this objview -> closed.
    auto unshare(bool wait, void *user_data) -> op_id;
    auto unshare(bool wait, completion c) -> op_id;

    // Event: key() = voucher token.
    auto create_voucher(std::uint32_t count, void *user_data) -> op_id;
    auto create_voucher(std::uint32_t count, completion c) -> op_id;

  private:
    friend auto internal::make_objview(
        std::shared_ptr<internal::objview_impl> impl) noexcept -> objview;
    friend auto internal::get_objview_impl(objview const &ov) noexcept
        -> std::shared_ptr<internal::objview_impl> const &;

    explicit objview(std::shared_ptr<internal::objview_impl> impl) noexcept;

    std::shared_ptr<internal::objview_impl> impl_;
};

} // namespace partake::client
