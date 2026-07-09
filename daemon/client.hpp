/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asio.hpp"

#include <gsl/span>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>

namespace partake::daemon {

// Lifetime: instances must be owned by shared_ptr. Every pending async
// operation's handler holds a keepalive shared_ptr to this client (never
// stored as a member, to avoid a reference cycle), so the client is
// destroyed only after all pending handlers have drained. close_self only
// removes the owner's reference; it is called exactly once, from the
// reader's end callback.
template <typename Socket, typename MessageReader, typename MessageWriter,
          typename Session, typename RequestHandler>
class client
    : public std::enable_shared_from_this<client<
          Socket, MessageReader, MessageWriter, Session, RequestHandler>> {
  public:
    using self_type = client;
    using socket_type = Socket;
    using message_reader_type = MessageReader;
    using message_writer_type = MessageWriter;
    using session_type = Session;
    using request_handler_type = RequestHandler;

  private:
    socket_type sock;
    session_type sess;
    message_writer_type writer;
    request_handler_type handler;
    message_reader_type reader;

    std::function<void(self_type &)> close_self;

  public:
    template <typename Segment, typename Allocator, typename Repository,
              typename HousekeepFunc, typename CloseFunc>
    explicit client(socket_type &&socket, std::uint32_t session_id,
                    Segment &seg, Allocator &allocator, Repository &repo,
                    std::chrono::milliseconds voucher_time_to_live,
                    HousekeepFunc per_req_housekeeping, CloseFunc close_client)
        : sock(std::forward<socket_type>(socket)),
          sess(session_id, seg, allocator, repo, voucher_time_to_live),
          writer(sock,
                 [this](std::error_code err) {
                     if (err)
                         handle_read_write_error(err);
                 }),
          handler(
              sess,
              [this](auto &&buf) {
                  writer.async_write_message(std::forward<decltype(buf)>(buf),
                                             this->shared_from_this());
              },
              std::move(per_req_housekeeping),
              [this](std::error_code err) { handle_read_write_error(err); }),
          reader(
              sock,
              [&handler = handler](gsl::span<std::uint8_t const> bytes) {
                  return handler.handle_message(bytes);
              },
              [this](std::error_code err) {
                  if (err)
                      handle_read_write_error(err);
                  else
                      handle_end_of_read();
                  close_self(*this);
              }),
          close_self(std::move(close_client)) {}

    // No move or copy (member references taken)
    ~client() = default;
    client(client const &) = delete;
    auto operator=(client const &) = delete;
    client(client &&) = delete;
    auto operator=(client &&) = delete;

    // Must not be called until this client is owned by a shared_ptr.
    void start() { reader.start(this->shared_from_this()); }

    void prepare_for_shutdown() { sess.drop_pending_requests(); }

    void close() {
        boost::system::error_code ignore;
        sock.shutdown(asio::socket_base::shutdown_type::shutdown_both, ignore);
        sock.close(ignore); // Cancel all async reads/writes.
    }

  private:
    void handle_end_of_read() {
        boost::system::error_code ignore;
        sock.shutdown(asio::socket_base::shutdown_type::shutdown_receive,
                      ignore);
        sess.drop_pending_requests();
        // Allow any in-flight writes to complete. This allows the client to
        // receive responses from requests sent before a 'Quit'.
    }

    void handle_read_write_error(std::error_code err) {
        if (err == boost::system::error_code(asio::error::operation_aborted))
            return;

        spdlog::error(
            "client {} (pid {}, \"{}\"): failed to read from or write to socket: {} ({})",
            sess.session_id(), sess.pid(), sess.name(), err.message(),
            err.value());

        boost::system::error_code ignore;
        sock.shutdown(asio::socket_base::shutdown_type::shutdown_both, ignore);
        sess.drop_pending_requests();
        sock.close(); // Cancel all async read/writes.
    }
};

} // namespace partake::daemon
