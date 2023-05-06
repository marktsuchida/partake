/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "allocator.hpp"
#include "asio.hpp"
#include "client.hpp"
#include "connection_acceptor.hpp"
#include "handle.hpp"
#include "hive.hpp"
#include "message.hpp"
#include "object.hpp"
#include "overloaded.hpp"
#include "quitter.hpp"
#include "repository.hpp"
#include "request_handler.hpp"
#include "segment.hpp"
#include "session.hpp"

#include <tl/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace partake::daemon {

struct daemon_config {
    asio::local::stream_protocol::endpoint endpoint;
    segment_config seg_config;
    std::size_t log2_granularity = 0;
};

template <typename AsioContext> class partake_daemon {
  private:
    using io_context_type = AsioContext;
    using socket_type = asio::local::stream_protocol::socket;
    using message_reader_type = async_message_reader<socket_type>;
    using message_writer_type =
        async_message_writer<socket_type, flatbuffers::DetachedBuffer>;
    using object_type = object<arena_allocator::allocation>;
    using voucher_queue_type = voucher_queue<object_type>;
    using repository_type =
        repository<object_type, token_sequence, voucher_queue_type>;
    using handle_type = handle<object_type>;
    using session_type =
        session<arena_allocator, repository_type, handle_type, segment>;
    using request_handler_type = request_handler<session_type>;
    using client_type =
        client<socket_type, message_reader_type, message_writer_type,
               session_type, request_handler_type>;

    quitter<io_context_type> quitr;
    connection_acceptor<asio::local::stream_protocol, io_context_type>
        acceptor;

    segment seg;
    arena_allocator allocr;

    steady_clock_traits clk_traits;
    voucher_queue_type vq;
    repository_type repo;

    hive<client_type> clients;
    std::uint32_t session_counter = 0;

    int exitcode = 0;

  public:
    explicit partake_daemon(io_context_type &asio_context,
                            daemon_config const &config) noexcept
        : quitr(asio_context, [this]() noexcept { acceptor.close(); }),
          acceptor(asio_context, config.endpoint), seg(config.seg_config),
          allocr(config.seg_config.size, config.log2_granularity),
          clk_traits(asio_context), vq(clk_traits),
          repo(token_sequence(), vq) {}

    // No move or copy (references to members are taken)
    auto operator=(partake_daemon &&) = delete;

    void start() noexcept {
        if (not acceptor.start(
                [this](socket_type &&sock) noexcept {
                    start_client(std::move(sock));
                },
                [this]() noexcept { quit(); }))
            exitcode = 1;
        else
            quitr.start();
    }

    auto exit_code() const noexcept -> int { return exitcode; }

  private:
    void start_client(socket_type &&socket) noexcept {
        clients
            .emplace(
                std::move(socket), session_counter++, seg, allocr, repo,
                [this]() noexcept { repo.perform_housekeeping(); },
                [this](client_type &c) noexcept {
                    clients.erase(clients.get_iterator(&c));
                })
            ->start();
    }

    void quit() noexcept {
        quitr.stop();

        // Drop pending requests before closing sessions (and hence
        // handles, objects), so that none of them resume.
        for (auto &c : clients)
            c.prepare_for_shutdown();
        clients.clear();

        repo.drop_all_vouchers();
    }
};

} // namespace partake::daemon