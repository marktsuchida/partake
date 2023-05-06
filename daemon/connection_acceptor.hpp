/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "asio.hpp"
#include "posix.hpp"
#include "win32.hpp"

#include <gsl/pointers>
#include <spdlog/spdlog.h>

#include <functional>
#include <memory>
#include <utility>

namespace partake::daemon {

template <typename StreamProtocol, typename AsioContext = asio::io_context>
class connection_acceptor {
  public:
    using io_context_type = AsioContext;
    using endpoint_type = typename StreamProtocol::endpoint;
    using socket_type = typename StreamProtocol::socket;

  private:
#ifdef _WIN32
    using unlinkable_type = typename win32::unlinkable;
#else
    using unlinkable_type = typename posix::unlinkable;
#endif

    gsl::not_null<io_context_type *> asio_context;
    endpoint_type endpt;
    typename StreamProtocol::acceptor acceptor;
    socket_type next_sock; // Unopened socket for next connection
    std::function<void(socket_type &&)> handle_new_conn;
    std::function<void()> handle_close_acceptor;
    unlinkable_type sock_unlinkable;
    bool closing = false;

  public:
    explicit connection_acceptor(io_context_type &context,
                                 endpoint_type endpoint) noexcept
        : asio_context(&context), endpt(std::move(endpoint)),
          acceptor(context), next_sock(context) {}

    auto start(std::function<void(socket_type &&)> handle_connection,
               std::function<void()> handle_close) noexcept -> bool {
        boost::system::error_code err;

        acceptor.open(endpt.protocol(), err);
        if (err) {
            spdlog::error("failed to open listening socket: {} ({})",
                          err.message(), err.value());
            return false;
        }

        acceptor.bind(endpt, err);
        if (err) {
            spdlog::error(
                "failed to bind listening socket to endpoint: {}: {} ({})",
                endpt.path(), err.message(), err.value());
            return false;
        }

        sock_unlinkable = unlinkable_type(endpt.path());

        acceptor.listen(socket_type::max_listen_connections, err);
        if (err) {
            spdlog::error("failed to listen on socket: {}: {} ({})",
                          endpt.path(), err.message(), err.value());
            return false;
        }
        spdlog::info("listening on socket: {}", endpt.path());

        handle_new_conn = std::move(handle_connection);
        handle_close_acceptor = std::move(handle_close);

        schedule_accept();
        return true;
    }

    void close() noexcept {
        if (closing)
            return;
        closing = true;

        sock_unlinkable.unlink();

        boost::system::error_code err;
        acceptor.close(err);
        if (err)
            spdlog::error("error while closing listening socket: {}: {} ({})",
                          endpt.path(), err.message(), err.value());
        else
            spdlog::info("closed listening socket: {}", endpt.path());

        // A connection may have been accepted asynchronously before the close.
        if (next_sock.is_open()) {
            boost::system::error_code ignore;
            next_sock.shutdown(socket_type::shutdown_both, ignore);
            next_sock.close(ignore);
        }

        if (handle_close_acceptor)
            handle_close_acceptor();
    }

  private:
    void schedule_accept() noexcept {
        acceptor.async_accept(
            next_sock, [this](boost::system::error_code const &err) noexcept {
                if (err) {
                    if (!closing) {
                        spdlog::error("failed to accept connection: {} ({})",
                                      err.message(), err.value());
                        close();
                    }
                } else {
                    handle_new_conn(
                        std::exchange(next_sock, socket_type(*asio_context)));
                    schedule_accept();
                }
            });
    }
};

} // namespace partake::daemon
