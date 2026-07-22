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

class connection;

namespace internal {

class connection_impl;

auto make_connection(std::shared_ptr<connection_impl> impl) noexcept
    -> connection;

auto get_connection_impl(connection const &c) noexcept
    -> std::shared_ptr<connection_impl> const &;

} // namespace internal

struct alloc_options {
    policy pol = policy::default_;
};

struct open_options {
    policy pol = policy::default_;
    bool wait = true;
};

// A connection to the daemon, obtained from a client::connect() completion
// event.
//
// Copyable handle (shared impl); dropping handles does NOT disconnect --
// the I/O side stays alive until shutdown() completes or the client dies.
//
// An empty (default-constructed) connection answers only operator bool();
// any other use is a programmer error (terminates).
class connection {
  public:
    connection() noexcept = default; // Empty.

    [[nodiscard]] explicit operator bool() const noexcept;

    // conn_no from ServerHelloMessage; diagnostic use only.
    [[nodiscard]] auto connection_number() const -> std::uint32_t;

    // Each submit returns an op_id immediately and has (..., void
    // *user_data) and (..., completion) overloads; the completion event is
    // delivered to the queue this connection is bound to.

    auto ping(void *user_data) -> op_id;
    auto ping(completion c) -> op_id;

    // Event: object() = writable objview, zeroed().
    auto alloc(std::uint64_t size, alloc_options options, void *user_data)
        -> op_id;
    auto alloc(std::uint64_t size, alloc_options options, completion c)
        -> op_id;

    // Event: object() = objview (read-only for default_ policy).
    auto open(token key, open_options options, void *user_data) -> op_id;
    auto open(token key, open_options options, completion c) -> op_id;

    // key may itself be a voucher (resolved to its target); count = the
    // allowed number of redemptions (open or discard_voucher).
    // Event: key() = the voucher key.
    auto create_voucher(token key, std::uint32_t count, void *user_data)
        -> op_id;
    auto create_voucher(token key, std::uint32_t count, completion c) -> op_id;

    // Expires the voucher; an ordinary object key is a no-op success.
    // Event: key() = the object key (diagnostic use only).
    auto discard_voucher(token key, void *user_data) -> op_id;
    auto discard_voucher(token key, completion c) -> op_id;

    // Graceful Quit handshake; after its completion event, this connection
    // emits no further events.
    auto shutdown(void *user_data) -> op_id;
    auto shutdown(completion c) -> op_id;

    // Detach interest in the op; it still potentially runs to completion
    // and delivers exactly one terminal event, with error
    // client_errc::canceled and no payload (a would-be alloc/open/unshare
    // result is auto-closed; a would-be voucher is auto-discarded).
    void cancel(op_id id);

  private:
    friend auto internal::make_connection(
        std::shared_ptr<internal::connection_impl> impl) noexcept
        -> connection;
    friend auto internal::get_connection_impl(connection const &c) noexcept
        -> std::shared_ptr<internal::connection_impl> const &;

    explicit connection(
        std::shared_ptr<internal::connection_impl> impl) noexcept;

    std::shared_ptr<internal::connection_impl> impl_;
};

} // namespace partake::client
