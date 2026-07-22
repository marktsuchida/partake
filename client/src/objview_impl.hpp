/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "partake/objview.hpp"
#include "partake/token.hpp"
#include "partake/types.hpp"

#include "event_impl.hpp"
#include "mapping.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace partake::client::internal {

class connection_impl;

// The state behind a (shared) public objview handle. All fields are
// immutable after construction except st, which is atomic because the
// destructor and data() may run on any thread (all state transitions happen
// on the I/O thread, in connection_impl::run_close, run_share, and
// run_unshare).
class objview_impl : public std::enable_shared_from_this<objview_impl> {
  public:
    enum class obj_state { open, closing, closed };

  private:
    std::shared_ptr<connection_impl> conn;
    std::shared_ptr<mapping> map; // Keeps the whole-segment mapping alive.
    std::uint64_t offset_ = 0;
    std::uint64_t size_ = 0;
    bool writable_ = false;
    token key_;
    std::atomic<obj_state> st{obj_state::open};

  public:
    objview_impl(std::shared_ptr<connection_impl> connection,
                 std::shared_ptr<mapping> segment_mapping,
                 std::uint64_t offset, std::uint64_t size, bool writable,
                 token key);

    // If still open, submits a suppressed fire-and-forget close (any-thread
    // safe). No race with an in-flight explicit close: that close keeps this
    // impl alive via the shared_ptr captured at submit time, so the
    // destructor cannot observe 'open' while it is pending.
    ~objview_impl();

    objview_impl(objview_impl const &) = delete;
    auto operator=(objview_impl const &) -> objview_impl & = delete;
    objview_impl(objview_impl &&) = delete;
    auto operator=(objview_impl &&) -> objview_impl & = delete;

    [[nodiscard]] auto key() const noexcept -> token { return key_; }
    [[nodiscard]] auto size() const noexcept -> std::uint64_t { return size_; }
    [[nodiscard]] auto writable() const noexcept -> bool { return writable_; }
    [[nodiscard]] auto data() const noexcept -> void *;

    auto submit_close(event_payload pl) -> op_id;
    auto submit_share(event_payload pl) -> op_id;
    auto submit_unshare(bool wait, event_payload pl) -> op_id;

    // A sibling views the same bytes (same connection, mapping, offset,
    // size) under a new writability and key; used by share/unshare, whose
    // success closes the source view.
    [[nodiscard]] auto make_sibling(bool writable, token new_key)
        -> std::shared_ptr<objview_impl>;

    // I/O thread only, called by the connection_impl run coroutines:
    auto begin_close() -> bool; // CAS open -> closing; false if lost.
    void mark_closed();
    void revert_close(); // closing -> open (failed share/unshare).
};

} // namespace partake::client::internal
