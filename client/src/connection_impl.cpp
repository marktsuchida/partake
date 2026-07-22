/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "connection_impl.hpp"

#include "objview_impl.hpp"

#include <spdlog/spdlog.h>

#include <cassert>
#include <exception>
#include <stdexcept>
#include <vector>

namespace partake::client::internal {

namespace {

[[nodiscard]] auto is_operation_canceled(std::error_code ec) noexcept -> bool {
    static auto const aborted =
        std::error_code(make_error_code(asio::error::operation_aborted));
    return ec == aborted;
}

// Our own teardown racing the connect surfaces as operation_aborted; report
// it as connect_failed like any other pre-ready failure.
[[nodiscard]] auto map_connect_error(std::error_code ec) noexcept
    -> std::error_code {
    if (is_operation_canceled(ec))
        return make_error_code(client_errc::connect_failed);
    return ec;
}

[[nodiscard]] auto suppressed_close_payload() -> event_payload {
    event_payload pl;
    pl.type = op_type::close;
    pl.suppress = true;
    return pl;
}

[[nodiscard]] auto suppressed_discard_voucher_payload() -> event_payload {
    event_payload pl;
    pl.type = op_type::discard_voucher;
    pl.suppress = true;
    return pl;
}

} // namespace

connection_impl::connection_impl(std::shared_ptr<client_impl> cl,
                                 std::shared_ptr<queue_impl> q)
    : clim(std::move(cl)), qimpl(std::move(q)), sock(clim->context()),
      writer(sock,
             [this](std::error_code ec) { handle_write_completion(ec); }),
      reader(
          sock,
          [this](gsl::span<std::uint8_t const> frame) {
              return handle_frame(frame);
          },
          [this](std::error_code ec) { handle_read_end(ec); }),
      seg_cache([this](std::uint32_t no) -> asio::awaitable<segment_spec> {
          co_return co_await request<segment_spec>(
              [no](request_builder &rb, std::uint64_t seqno) {
                  add_get_segment_request(rb, seqno, no);
              },
              decode_get_segment_response);
      }) {}

void connection_impl::start_connect(std::string socket_path, std::string name,
                                    event_payload pl) {
    clim->adopt_connection(shared_from_this());
    asio::co_spawn(clim->context(),
                   run_connect(shared_from_this(), std::move(socket_path),
                               std::move(name), std::move(pl)),
                   asio::detached);
}

auto connection_impl::submit_ping(event_payload pl) -> op_id {
    return submit_op(std::move(pl), run_ping);
}

auto connection_impl::submit_alloc(std::uint64_t size, alloc_options opts,
                                   event_payload pl) -> op_id {
    auto const policy = to_protocol_policy(opts.pol);
    return submit_op(std::move(pl),
                     [size, policy](std::shared_ptr<connection_impl> self,
                                    op_id id, event_payload payload) {
                         return run_alloc(std::move(self), id, size, policy,
                                          std::move(payload));
                     });
}

auto connection_impl::submit_open(token key, open_options opts,
                                  event_payload pl) -> op_id {
    auto const policy = to_protocol_policy(opts.pol);
    auto const wait = opts.wait;
    return submit_op(std::move(pl),
                     [key, policy, wait](std::shared_ptr<connection_impl> self,
                                         op_id id, event_payload payload) {
                         return run_open(std::move(self), id, key.as_u64(),
                                         policy, wait, std::move(payload));
                     });
}

auto connection_impl::submit_close(token key, std::shared_ptr<objview_impl> ov,
                                   event_payload pl) -> op_id {
    return submit_op(std::move(pl), [key, ov = std::move(ov)](
                                        std::shared_ptr<connection_impl> self,
                                        op_id id, event_payload payload) {
        return run_close(std::move(self), id, key, ov, std::move(payload));
    });
}

auto connection_impl::submit_share(std::shared_ptr<objview_impl> ov,
                                   event_payload pl) -> op_id {
    return submit_op(std::move(pl), [ov = std::move(ov)](
                                        std::shared_ptr<connection_impl> self,
                                        op_id id, event_payload payload) {
        return run_share(std::move(self), id, ov, std::move(payload));
    });
}

auto connection_impl::submit_unshare(std::shared_ptr<objview_impl> ov,
                                     bool wait, event_payload pl) -> op_id {
    return submit_op(std::move(pl), [ov = std::move(ov), wait](
                                        std::shared_ptr<connection_impl> self,
                                        op_id id, event_payload payload) {
        return run_unshare(std::move(self), id, ov, wait, std::move(payload));
    });
}

auto connection_impl::submit_create_voucher(token key, std::uint32_t count,
                                            std::shared_ptr<objview_impl> ov,
                                            event_payload pl) -> op_id {
    return submit_op(std::move(pl), [key, count, ov = std::move(ov)](
                                        std::shared_ptr<connection_impl> self,
                                        op_id id, event_payload payload) {
        return run_create_voucher(std::move(self), id, key, count, ov,
                                  std::move(payload));
    });
}

auto connection_impl::submit_discard_voucher(token key, event_payload pl)
    -> op_id {
    return submit_op(std::move(pl),
                     [key](std::shared_ptr<connection_impl> self, op_id id,
                           event_payload payload) {
                         return run_discard_voucher(std::move(self), id, key,
                                                    std::move(payload));
                     });
}

auto connection_impl::submit_shutdown(event_payload pl) -> op_id {
    return submit_op(std::move(pl), run_shutdown);
}

void connection_impl::cancel(op_id id) {
    auto self = shared_from_this();
    // A failed post means the client has stopped; the op has then already
    // received (or will receive) its disconnected event, so cancel is moot.
    (void)clim->try_post([self, id] {
        auto it = self->ops.find(id);
        if (it != self->ops.end())
            it->second.detached = true;
    });
}

void connection_impl::quit() {
    teardown(make_error_code(client_errc::disconnected));
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
auto connection_impl::run_connect(std::shared_ptr<connection_impl> self,
                                  std::string socket_path, std::string name,
                                  event_payload pl) -> asio::awaitable<void> {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    try {
        // teardown() may have run before this coroutine started (a client
        // stop posted behind the connect submit), and can run again during
        // the connect await -- after which async_connect would have
        // (re)opened the closed socket and hello_waiter would never be
        // completed. Check before arming the hello wait.
        if (self->state != state_t::connecting)
            throw std::system_error(
                make_error_code(client_errc::connect_failed));
        auto const endpoint =
            asio::local::stream_protocol::endpoint(socket_path);
        co_await self->sock.async_connect(endpoint, asio::use_awaitable);
        if (self->state != state_t::connecting)
            throw std::system_error(
                make_error_code(client_errc::connect_failed));
        self->reader.start(self);
        self->writer.async_write_message(make_client_hello(name), self);
        co_await self->wait_server_hello();
        pl.conn = make_connection(self);
    } catch (boost::system::system_error const &e) {
        pl.error = map_connect_error(e.code());
    } catch (std::system_error const &e) {
        pl.error = map_connect_error(e.code());
    } catch (...) {
        pl.error = make_error_code(client_errc::connect_failed);
    }
    if (pl.error) {
        self->teardown(make_error_code(client_errc::disconnected));
        // If teardown had already run, it did not close a socket that
        // async_connect opened afterwards; close unconditionally.
        boost::system::error_code ignore;
        (void)self->sock.close(ignore);
    }
    self->qimpl->push(make_event(std::move(pl)));
}

auto connection_impl::run_ping(std::shared_ptr<connection_impl> self, op_id id,
                               event_payload pl) -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        co_await self->request<ping_result>(add_ping_request,
                                            decode_ping_response);
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::run_alloc(std::shared_ptr<connection_impl> self,
                                op_id id, std::uint64_t size,
                                protocol::Policy policy, event_payload pl)
    -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        auto const r = co_await self->request<alloc_result>(
            [size, policy](request_builder &rb, std::uint64_t seqno) {
                add_alloc_request(rb, seqno, size, policy);
            },
            decode_alloc_response);
        try {
            auto m = co_await self->get_segment_mapping(r.segment);
            pl.obj = make_objview(std::make_shared<objview_impl>(
                self, std::move(m), r.offset, r.size, /*writable=*/true,
                token(r.key)));
            pl.zeroed = r.zeroed;
        } catch (...) {
            // The wire alloc succeeded but we cannot use the object; close
            // it so it is not leaked on the daemon.
            (void)self->submit_close(token(r.key), nullptr,
                                     suppressed_close_payload());
            throw;
        }
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::run_open(std::shared_ptr<connection_impl> self, op_id id,
                               std::uint64_t key, protocol::Policy policy,
                               bool wait, event_payload pl)
    -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        // A deferred response (wait == true, object unshared) just parks
        // this await until the daemon replies.
        auto const r = co_await self->request<open_result>(
            [key, policy, wait](request_builder &rb, std::uint64_t seqno) {
                add_open_request(rb, seqno, key, policy, wait);
            },
            decode_open_response);
        try {
            auto m = co_await self->get_segment_mapping(r.segment);
            pl.obj = make_objview(std::make_shared<objview_impl>(
                self, std::move(m), r.offset, r.size,
                policy == protocol::Policy::PRIMITIVE, token(r.key)));
        } catch (...) {
            (void)self->submit_close(token(r.key), nullptr,
                                     suppressed_close_payload());
            throw;
        }
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::run_close(std::shared_ptr<connection_impl> self,
                                op_id id, token key,
                                std::shared_ptr<objview_impl> ov,
                                event_payload pl) -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        if (ov and not ov->begin_close())
            throw std::system_error(
                make_error_code(client_errc::object_closed));
        try {
            co_await self->request<close_result>(
                [key](request_builder &rb, std::uint64_t seqno) {
                    add_close_request(rb, seqno, key.as_u64());
                },
                decode_close_response);
        } catch (...) {
            // A failed close means the connection is dead or dying (the
            // daemon reclaims per-connection handles on disconnect); mark
            // closed anyway so data() stays null and the destructor does not
            // resubmit.
            if (ov)
                ov->mark_closed();
            throw;
        }
        if (ov)
            ov->mark_closed();
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::run_share(std::shared_ptr<connection_impl> self,
                                op_id id, std::shared_ptr<objview_impl> ov,
                                event_payload pl) -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        if (not ov->begin_close())
            throw std::system_error(
                make_error_code(client_errc::object_closed));
        try {
            auto const key = ov->key();
            co_await self->request<share_result>(
                [key](request_builder &rb, std::uint64_t seqno) {
                    add_share_request(rb, seqno, key.as_u64());
                },
                decode_share_response);
        } catch (...) {
            // The source view remains usable on failure.
            ov->revert_close();
            throw;
        }
        // On success our access downgrades to read-only under the same key;
        // replace the source view with a read-only sibling.
        ov->mark_closed();
        pl.obj = make_objview(ov->make_sibling(false, ov->key()));
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::run_unshare(std::shared_ptr<connection_impl> self,
                                  op_id id, std::shared_ptr<objview_impl> ov,
                                  bool wait, event_payload pl)
    -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        if (not ov->begin_close())
            throw std::system_error(
                make_error_code(client_errc::object_closed));
        unshare_result r;
        try {
            auto const key = ov->key();
            // A deferred response (wait == true, object not exclusively
            // held) just parks this await until the daemon replies.
            r = co_await self->request<unshare_result>(
                [key, wait](request_builder &rb, std::uint64_t seqno) {
                    add_unshare_request(rb, seqno, key.as_u64(), wait);
                },
                decode_unshare_response);
        } catch (...) {
            // The source view remains usable on failure (e.g. OBJECT_BUSY
            // with wait == false, which the caller may retry).
            ov->revert_close();
            throw;
        }
        // On success the daemon rekeyed the same object in place and
        // reopened it read+write; only the key changed.
        ov->mark_closed();
        pl.obj = make_objview(ov->make_sibling(true, token(r.key)));
        pl.key = token(r.key);
        pl.zeroed = r.zeroed;
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::run_create_voucher(std::shared_ptr<connection_impl> self,
                                         op_id id, token key,
                                         std::uint32_t count,
                                         std::shared_ptr<objview_impl> ov,
                                         event_payload pl)
    -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        // ov is null when invoked with a bare key rather than an objview.
        if (ov and not ov->is_open())
            throw std::system_error(
                make_error_code(client_errc::object_closed));
        auto const r = co_await self->request<create_voucher_result>(
            [key, count](request_builder &rb, std::uint64_t seqno) {
                add_create_voucher_request(rb, seqno, key.as_u64(), count);
            },
            decode_create_voucher_response);
        pl.key = token(r.key);
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::run_discard_voucher(
    std::shared_ptr<connection_impl> self, op_id id, token key,
    event_payload pl) -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        auto const r = co_await self->request<discard_voucher_result>(
            [key](request_builder &rb, std::uint64_t seqno) {
                add_discard_voucher_request(rb, seqno, key.as_u64());
            },
            decode_discard_voucher_response);
        pl.key = token(r.key); // The object key, for diagnostics only.
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::run_shutdown(std::shared_ptr<connection_impl> self,
                                   op_id id, event_payload pl)
    -> asio::awaitable<void> {
    try {
        if (self->state != state_t::ready)
            throw std::system_error(
                make_error_code(client_errc::disconnected));
        self->state = state_t::closing;
        try {
            co_await self->request<quit_result>(add_quit_request,
                                                decode_quit_response);
        } catch (...) { // NOLINT(bugprone-empty-catch)
            // The server may close without sending a QuitResponse; teardown
            // is achieved either way, so shutdown still succeeds. (A failed
            // Quit *write* is indistinguishable from that and also reports
            // success -- best effort.)
        }
        self->teardown(make_error_code(client_errc::disconnected));
    } catch (std::system_error const &e) {
        pl.error = e.code();
    } catch (...) {
        pl.error = make_error_code(client_errc::disconnected);
    }
    self->finish(id, std::move(pl));
}

auto connection_impl::wait_server_hello() -> asio::awaitable<void> {
    auto token = asio::as_tuple(asio::use_awaitable);
    auto [ec] =
        co_await asio::async_initiate<decltype(token), void(std::error_code)>(
            [this](auto handler) {
                assert(not hello_waiter);
                hello_waiter =
                    unique_handler<void(std::error_code)>(std::move(handler));
            },
            token);
    if (ec)
        throw std::system_error(ec);
}

auto connection_impl::get_segment_mapping(std::uint32_t seg_no)
    -> asio::awaitable<std::shared_ptr<mapping>> {
    co_return co_await seg_cache.get(seg_no);
}

auto connection_impl::handle_frame(gsl::span<std::uint8_t const> frame)
    -> bool {
    switch (state) {
    case state_t::connecting:
        return handle_hello_frame(frame);
    case state_t::ready:
    case state_t::closing:
        return handle_response_frame(frame);
    case state_t::closed:
        return true;
    }
    assert(false);
    std::terminate();
}

auto connection_impl::handle_hello_frame(gsl::span<std::uint8_t const> frame)
    -> bool {
    if (not hello_waiter)
        return true; // Stray frame after an already-failed hello.
    if (not verify_server_hello_message(frame)) {
        hello_waiter(make_error_code(client_errc::connect_failed));
        return true;
    }
    auto const *hello =
        flatbuffers::GetSizePrefixedRoot<protocol::ServerHelloMessage>(
            frame.data());
    if (hello->result() == protocol::HelloResult::OK) {
        // Set ready before completing the waiter: resumption may be
        // deferred, and the next frame must dispatch as a response.
        conn_no = hello->conn_no();
        state = state_t::ready;
        hello_waiter(std::error_code());
    } else {
        // The server flushes the rejection and closes.
        hello_waiter(
            make_error_code(client_errc::unsupported_protocol_version));
    }
    return false;
}

auto connection_impl::handle_response_frame(
    gsl::span<std::uint8_t const> frame) -> bool {
    if (not verify_response_message(frame)) {
        teardown(make_error_code(client_errc::protocol_violation));
        return true;
    }
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
            frame.data());
    auto const *responses = msg->responses();
    if (responses == nullptr)
        return false;
    for (auto const *resp : *responses) {
        auto const seqno = resp->seqno();
        pending_cont cont;
        try {
            cont = std::move(pending.peek(seqno));
        } catch (std::runtime_error const &) {
            teardown(make_error_code(client_errc::protocol_violation));
            return true;
        }
        pending.pop(seqno);
        bool decoded_ok = true;
        if (resp->status() != protocol::Status::OK)
            (void)cont(nullptr, error_code_for_status(resp->status()));
        else
            decoded_ok = cont(resp, {});
        if (not decoded_ok) {
            teardown(make_error_code(client_errc::protocol_violation));
            return true;
        }
        // A resumed continuation may have torn the connection down (e.g.
        // the QuitResponse resuming run_shutdown).
        if (state == state_t::closed)
            return true;
    }
    return false;
}

// The reader reports clean EOF as a success code; mid-frame EOF and other
// read errors arrive as nonzero.
void connection_impl::handle_read_end(std::error_code ec) {
    switch (state) {
    case state_t::connecting:
        if (ec and not is_operation_canceled(ec)) {
            spdlog::error("connection {}: read error before server hello: "
                          "{}",
                          conn_no, ec.message());
        }
        if (hello_waiter)
            hello_waiter(make_error_code(client_errc::connect_failed));
        return;
    case state_t::ready:
    case state_t::closing:
        if (ec and not is_operation_canceled(ec))
            spdlog::error("connection {}: read error: {}", conn_no,
                          ec.message());
        return teardown(make_error_code(client_errc::disconnected));
    case state_t::closed:
        return; // Typically operation_aborted from our own close.
    }
    assert(false);
    std::terminate();
}

void connection_impl::handle_write_completion(std::error_code ec) {
    if (not ec or is_operation_canceled(ec))
        return;
    spdlog::error("connection {}: write error: {}", conn_no, ec.message());
    teardown(make_error_code(client_errc::disconnected));
}

void connection_impl::finish(op_id id, event_payload pl) {
    bool detached = false;
    auto it = ops.find(id);
    if (it != ops.end()) {
        detached = it->second.detached;
        ops.erase(it);
    }
    if (detached) {
        // Auto-discard an abandoned voucher, so it does not linger until
        // its TTL (the voucher analogue of the auto-close below).
        if (pl.type == op_type::create_voucher and pl.key.is_valid())
            (void)submit_discard_voucher(pl.key,
                                         suppressed_discard_voucher_payload());
        // Auto-close an abandoned alloc/open/unshare result: dropping the
        // payload's (only) objview handle triggers ~objview_impl's
        // fire-and-forget close, posted as a later task.
        pl.obj = objview();
        pl.key = token();
        pl.zeroed = false;
        pl.error = make_error_code(client_errc::canceled);
    }
    if (not pl.suppress)
        qimpl->push(make_event(std::move(pl)));
}

// Every terminal path funnels through here (idempotent). ec is what pending
// ops fail with; a pending connect always fails with connect_failed.
void connection_impl::teardown(std::error_code ec) {
    if (state == state_t::closed)
        return;
    state = state_t::closed;
    if (hello_waiter)
        hello_waiter(make_error_code(client_errc::connect_failed));
    boost::system::error_code ignore;
    (void)sock.shutdown(socket_type::shutdown_both, ignore);
    (void)sock.close(ignore);
    // Collect-then-invoke: resumed continuations may re-enter (submit,
    // teardown) and must not observe a half-cleared pending map.
    std::vector<std::pair<std::uint64_t, pending_cont>> conts;
    pending.for_each([&conts](std::uint64_t seqno, pending_cont &cont) {
        conts.emplace_back(seqno, std::move(cont));
    });
    for (auto const &sc : conts)
        pending.pop(sc.first);
    for (auto &[seqno, cont] : conts)
        (void)cont(nullptr, ec);
    seg_cache.clear();
    // May drop the registry's strong reference; every caller holds a
    // keepalive.
    clim->drop_connection(this);
}

} // namespace partake::client::internal
