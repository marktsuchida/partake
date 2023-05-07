/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "allocator.hpp"
#include "asio.hpp"
#include "segment.hpp"

#include <gsl/span>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <utility>

namespace partake::daemon {

template <typename Socket, typename MessageReader, typename MessageWriter,
          typename Session, typename RequestHandler>
class client {
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

    // Number of async read/write operations in flight. This could be replaced
    // with an automatic refcounting token/pointer, but manual refcounting is
    // simple enough for now since message reading and writing always goes
    // through the client object.
    std::size_t io_refcount = 0;
    std::function<void(self_type &)> close_self;

  public:
    template <typename Repository, typename HousekeepFunc, typename CloseFunc>
    explicit client(socket_type &&socket, std::uint32_t session_id,
                    segment &seg, arena_allocator &allocator, Repository &repo,
                    std::chrono::milliseconds voucher_time_to_live,
                    HousekeepFunc per_req_housekeeping,
                    CloseFunc close_client) noexcept
        : sock(std::forward<socket_type>(socket)),
          sess(session_id, seg, allocator, repo, voucher_time_to_live),
          writer(sock,
                 [this](std::error_code err) noexcept {
                     if (err)
                         handle_read_write_error(err);
                     decrement_io_refcount();
                 }),
          handler(
              sess,
              [this](auto &&buf) noexcept {
                  increment_io_refcount();
                  writer.async_write_message(std::forward<decltype(buf)>(buf));
              },
              std::move(per_req_housekeeping),
              [this](std::error_code err) noexcept {
                  handle_read_write_error(err);
              }),
          reader(
              sock,
              [&handler =
                   handler](gsl::span<std::uint8_t const> bytes) noexcept {
                  return handler.handle_message(bytes);
              },
              [this](std::error_code err) noexcept {
                  if (err)
                      handle_read_write_error(err);
                  else
                      handle_end_of_read();
                  decrement_io_refcount();
              }),
          close_self(std::move(close_client)) {}

    // No move or copy (member references taken)
    auto operator=(client &&) = delete;

    void start() noexcept {
        reader.start();
        increment_io_refcount();
    }

    void prepare_for_shutdown() noexcept { sess.drop_pending_requests(); }

  private:
    void handle_end_of_read() noexcept {
        boost::system::error_code ignore;
        sock.shutdown(asio::socket_base::shutdown_type::shutdown_receive,
                      ignore);
        sess.drop_pending_requests();
        // Allow any in-flight writes to complete. This allows the client to
        // receive responses from requests sent before a 'Quit'.
    }

    void handle_read_write_error(std::error_code err) noexcept {
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

    void increment_io_refcount() noexcept { ++io_refcount; }

    void decrement_io_refcount() noexcept {
        --io_refcount;
        if (io_refcount == 0) {
            close_self(*this);
        }
    }
};

} // namespace partake::daemon
