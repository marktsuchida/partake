/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "partake/connection.hpp"
#include "partake/errors.hpp"
#include "partake/token.hpp"
#include "partake/types.hpp"

#include "asio.hpp"
#include "client_impl.hpp"
#include "event_impl.hpp"
#include "mapping.hpp"
#include "message.hpp"
#include "queue_impl.hpp"
#include "request_builder.hpp"
#include "requests.hpp"
#include "segment_cache.hpp"
#include "seqno_map.hpp"
#include "unique_handler.hpp"

#include <flatbuffers/flatbuffers.h>
#include <gsl/span>

#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace partake::client::internal {

class objview_impl;

// The per-connection I/O core. All state is confined to the client's I/O
// thread; the submit_*() and cancel() members may be called from any thread
// (they bounce through client_impl::try_post(), and deliver the terminal event
// from the submitting thread when the client has stopped).
//
// Lifetime: client_impl and connection_impl deliberately hold strong
// references to each other, each edge for its own reason. A connection keeps
// its client_impl alive because the executor and I/O thread it runs on live
// there. The client's connection registry (held from start_connect() until
// teardown()) lets ~client() abort every live connection, and is why dropping
// public handles does not disconnect. The cycle cannot leak: the public
// client is a value whose destructor and move assignment both funnel into
// client_impl::quit(), whose teardown of every connection -- the funnel for
// all terminal paths (shutdown completion, fatal error, protocol-violation
// panic, client-quit) -- clears the registry, breaking the cycle. Pending asio
// handlers and op coroutines each hold their own shared_ptr keepalive per the
// convention documented in common/message.hpp.
class connection_impl : public std::enable_shared_from_this<connection_impl> {
    using socket_type = asio::local::stream_protocol::socket;

    // Continuation of an in-flight request. Exactly one of resp/ec is set.
    // A false return means the decoder detected a protocol violation (the
    // read path then panics the connection; the continuation has already
    // completed its own op with protocol_violation).
    using pending_cont = unique_handler<
        auto(protocol::Response const *, std::error_code)->bool>;

    struct op_record {
        bool detached = false;
    };

    enum class state_t { connecting, ready, closing, closed };

    std::shared_ptr<client_impl> clim; // First: ctx outlives sock.
    std::shared_ptr<queue_impl> qimpl;
    socket_type sock;
    common::async_message_writer<socket_type, flatbuffers::DetachedBuffer>
        writer;
    common::async_message_reader<socket_type> reader;
    seqno_map<pending_cont> pending;
    std::unordered_map<op_id, op_record> ops; // I/O thread only.
    state_t state = state_t::connecting;      // I/O thread only.
    std::uint32_t conn_no = 0;
    unique_handler<void(std::error_code)> hello_waiter;
    segment_cache seg_cache; // I/O thread only.

  public:
    connection_impl(std::shared_ptr<client_impl> cl,
                    std::shared_ptr<queue_impl> q);

    ~connection_impl() = default;
    connection_impl(connection_impl const &) = delete;
    auto operator=(connection_impl const &) -> connection_impl & = delete;
    connection_impl(connection_impl &&) = delete;
    auto operator=(connection_impl &&) -> connection_impl & = delete;

    // I/O thread; registers with the client's connection registry and
    // spawns the connect coroutine, which pushes the completion event.
    void start_connect(std::string socket_path, std::string name,
                       event_payload pl);

    // Any thread.
    auto submit_ping(event_payload pl) -> op_id;
    auto submit_alloc(std::uint64_t size, alloc_options opts, event_payload pl)
        -> op_id;
    auto submit_open(token key, open_options opts, event_payload pl) -> op_id;
    // ov is null for the fire-and-forget paths (~objview_impl and the
    // alloc/open unwind), which carry a suppressed payload.
    auto submit_close(token key, std::shared_ptr<objview_impl> ov,
                      event_payload pl) -> op_id;
    auto submit_shutdown(event_payload pl) -> op_id;
    void cancel(op_id id);

    // Written on the I/O thread before the connect success event is pushed
    // (the queue mutex publishes it); stays readable after death.
    [[nodiscard]] auto connection_number() const noexcept -> std::uint32_t {
        return conn_no;
    }

    // I/O thread; called by client_impl::quit()'s task.
    void quit();

  private:
    // The awaitable request primitive. add_req(rb, seqno) serializes the
    // request; decode(resp) -> std::optional<Result> runs inside the reader
    // callback (the Response points into the read buffer and is valid only
    // there). Throws std::system_error on failure.
    template <typename Result, typename AddReq, typename Decode>
    auto request(AddReq add_req, Decode decode) -> asio::awaitable<Result> {
        auto token = asio::as_tuple(asio::use_awaitable);
        auto [ec, result] =
            co_await asio::async_initiate<decltype(token),
                                          void(std::error_code, Result)>(
                [this, &add_req, &decode](auto handler) {
                    this->initiate_request<Result>(std::move(handler),
                                                   std::move(add_req),
                                                   std::move(decode));
                },
                token);
        if (ec)
            throw std::system_error(ec);
        co_return result;
    }

    // I/O thread (runs inline in the op coroutine's resumption).
    template <typename Result, typename Handler, typename AddReq,
              typename Decode>
    void initiate_request(Handler handler, AddReq add_req, Decode decode) {
        if (state == state_t::closed) {
            // Needed for multi-request ops that can resume after a teardown():
            return asio::defer(
                sock.get_executor(), [h = std::move(handler)]() mutable {
                    std::move(h)(make_error_code(client_errc::disconnected),
                                 Result{});
                });
        }
        auto const seqno = pending.next_seqno();
        pending.push() =
            pending_cont([h = std::move(handler), decode = std::move(decode)](
                             protocol::Response const *resp,
                             std::error_code err) mutable -> bool {
                if (err) {
                    std::move(h)(err, Result{});
                    return true;
                }
                auto result = decode(*resp);
                if (not result) {
                    std::move(h)(
                        make_error_code(client_errc::protocol_violation),
                        Result{});
                    return false;
                }
                std::move(h)(std::error_code(), std::move(*result));
                return true;
            });
        auto rb = request_builder(1);
        add_req(rb, seqno);
        writer.async_write_message(rb.release_buffer(), shared_from_this());
    }

    // Shared submit bridge for ping and shutdown. Run is a static member
    // coroutine (self, id, pl) -> awaitable<void> that must end by calling
    // finish() exactly once.
    template <typename Run>
    auto submit_op(event_payload pl, Run run) -> op_id {
        auto const id = clim->next_op_id();
        pl.id = id;
        auto self = shared_from_this();
        bool const posted = clim->try_post([self, id, pl, run]() mutable {
            self->ops.emplace(id, op_record{});
            asio::co_spawn(self->clim->context(), run(self, id, std::move(pl)),
                           asio::detached);
        });
        if (not posted) {
            pl.error = make_error_code(client_errc::disconnected);
            if (not pl.suppress)
                qimpl->push(make_event(std::move(pl)));
        }
        return id;
    }

    static auto run_connect(std::shared_ptr<connection_impl> self,
                            std::string socket_path, std::string name,
                            event_payload pl) -> asio::awaitable<void>;
    static auto run_ping(std::shared_ptr<connection_impl> self, op_id id,
                         event_payload pl) -> asio::awaitable<void>;
    static auto run_alloc(std::shared_ptr<connection_impl> self, op_id id,
                          std::uint64_t size, protocol::Policy policy,
                          event_payload pl) -> asio::awaitable<void>;
    static auto run_open(std::shared_ptr<connection_impl> self, op_id id,
                         std::uint64_t key, protocol::Policy policy, bool wait,
                         event_payload pl) -> asio::awaitable<void>;
    static auto run_close(std::shared_ptr<connection_impl> self, op_id id,
                          token key, std::shared_ptr<objview_impl> ov,
                          event_payload pl) -> asio::awaitable<void>;
    static auto run_shutdown(std::shared_ptr<connection_impl> self, op_id id,
                             event_payload pl) -> asio::awaitable<void>;

    auto wait_server_hello() -> asio::awaitable<void>;

    // Fetch (via GetSegment) and cache the whole-segment mapping for seg_no.
    // To be consumed by alloc/open coroutines.
    auto get_segment_mapping(std::uint32_t seg_no)
        -> asio::awaitable<std::shared_ptr<mapping>>;

    auto handle_frame(gsl::span<std::uint8_t const> frame) -> bool;
    auto handle_hello_frame(gsl::span<std::uint8_t const> frame) -> bool;
    auto handle_response_frame(gsl::span<std::uint8_t const> frame) -> bool;
    void handle_read_end(std::error_code ec);
    void handle_write_completion(std::error_code ec);

    void finish(op_id id, event_payload pl);
    void teardown(std::error_code ec);
};

} // namespace partake::client::internal
