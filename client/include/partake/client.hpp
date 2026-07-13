/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event.hpp"
#include "queue.hpp"
#include "types.hpp"

#include <memory>
#include <string>

namespace partake::client {

class client;

namespace internal {

class client_impl;

auto get_client_impl(client const &c) noexcept
    -> std::shared_ptr<client_impl> const &;

} // namespace internal

struct client_options {
    // Reserved for the mapping-mode override and future knobs.
};

// Owns the background I/O thread on which all connections run.
class client {
  public:
    client(); // Starts the background I/O thread.
    explicit client(client_options const &options);

    // Quits remaining connections (failing their pending ops) and joins the
    // I/O thread. Move assignment does the same to the assigned-over
    // client.
    ~client();

    client(client const &) = delete;
    auto operator=(client const &) -> client & = delete;
    client(client &&) noexcept = default;
    auto operator=(client &&other) noexcept -> client &;

    // Async connect: returns op_id immediately; the completion event
    // carries the connection on success (event::get_connection()) or an
    // error (client_errc::connect_failed, unsupported_protocol_version, or
    // a system error). Connection close before the server hello arrives
    // (the server may close instead of replying to a rejected hello)
    // reports connect_failed. The connection is permanently bound to q; all
    // of its op completions (and those of its objviews) are delivered
    // there. The connect op is not cancelable (no connection handle exists
    // until the completion event).
    auto connect(std::string const &socket_path, std::string const &name,
                 queue &q, void *user_data) -> op_id;
    auto connect(std::string const &socket_path, std::string const &name,
                 queue &q, completion c) -> op_id;

  private:
    friend auto internal::get_client_impl(client const &c) noexcept
        -> std::shared_ptr<internal::client_impl> const &;

    std::shared_ptr<internal::client_impl> impl_;
};

} // namespace partake::client
