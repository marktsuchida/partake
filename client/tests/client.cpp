/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/client.hpp"

#include "asio.hpp"
#include "client_impl.hpp"
#include "mock_server.hpp"
#include "partake/connection.hpp"
#include "partake/errors.hpp"
#include "partake/event.hpp"
#include "partake/queue.hpp"
#include "partake/types.hpp"
#include "testing.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <poll.h>

namespace partake::client {

TEST_CASE("client: constructs and destructs without hanging") {
    SECTION("default constructor") { client const c; }

    SECTION("options constructor") { client const c((client_options())); }
}

TEST_CASE("client: posted tasks run on the I/O thread") {
    client const c;
    std::promise<std::thread::id> prom;
    auto fut = prom.get_future();
    asio::post(internal::get_client_impl(c)->context(),
               [&prom] { prom.set_value(std::this_thread::get_id()); });
    CHECK(fut.get() != std::this_thread::get_id());
}

TEST_CASE("client: op_id counter starts at 1 and increases") {
    client const c;
    auto const &impl = internal::get_client_impl(c);
    CHECK(impl->next_op_id() == 1);
    CHECK(impl->next_op_id() == 2);
    CHECK(impl->next_op_id() == 3);
}

TEST_CASE("client: op_ids are unique across threads") {
    static constexpr std::size_t n_threads = 4;
    static constexpr std::size_t per_thread = 1000;
    client const c;
    auto const &impl = internal::get_client_impl(c);

    std::vector<std::vector<op_id>> ids(n_threads);
    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (std::size_t t = 0; t < n_threads; ++t) {
        threads.emplace_back([&impl, &ids, t] {
            ids[t].reserve(per_thread);
            for (std::size_t i = 0; i < per_thread; ++i)
                ids[t].push_back(impl->next_op_id());
        });
    }
    for (auto &th : threads)
        th.join();

    std::set<op_id> unique;
    for (auto const &v : ids)
        unique.insert(v.begin(), v.end());
    CHECK(unique.size() == n_threads * per_thread);
    CHECK(unique.count(0) == 0);
}

namespace {

constexpr auto event_timeout = std::chrono::milliseconds(5000);

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("client: connect success delivers the connection via the event") {
    mock_server const srv;
    client c;
    queue q;

    SECTION("user_data overload") {
        int cookie = 0;
        auto const id = c.connect(srv.socket_path(), "test", q, &cookie);
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        CHECK(ev->id() == id);
        CHECK(ev->type() == op_type::connect);
        CHECK_FALSE(ev->error());
        CHECK(ev->user_data() == &cookie);
        auto conn = ev->get_connection();
        REQUIRE(conn);
        CHECK(conn.connection_number() == 42);
    }

    SECTION("completion overload, delivered via queue::dispatch") {
        std::optional<event> received;
        auto const id =
            c.connect(srv.socket_path(), "test", q,
                      [&received](event &&ev) { received = std::move(ev); });
        struct pollfd pfd = {};
        pfd.fd = q.wakeup();
        pfd.events = POLLIN;
        REQUIRE(::poll(&pfd, 1, static_cast<int>(event_timeout.count())) == 1);
        q.dispatch();
        REQUIRE(received.has_value());
        CHECK(received->id() == id);
        CHECK(received->type() == op_type::connect);
        CHECK_FALSE(received->error());
        auto conn = received->get_connection();
        REQUIRE(conn);
        CHECK(conn.connection_number() == 42);
    }
}

TEST_CASE("client: connect fails on hello rejection") {
    mock_server const srv({.hello = mock_server::hello_mode::reject});
    client c;
    queue q;
    auto const id = c.connect(srv.socket_path(), "test", q, nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::connect);
    CHECK(ev->error() == client_errc::unsupported_protocol_version);
    CHECK_FALSE(ev->get_connection());
}

TEST_CASE("client: connect fails when server closes without a reply") {
    mock_server const srv(
        {.hello = mock_server::hello_mode::close_without_reply});
    client c;
    queue q;
    auto const id = c.connect(srv.socket_path(), "test", q, nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error() == client_errc::connect_failed);
    CHECK_FALSE(ev->get_connection());
}

TEST_CASE("client: connect to nonexistent path reports a system error") {
    testing::tempdir const td;
    auto const path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    client c;
    queue q;
    auto const id = c.connect(path.string(), "test", q, nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error());
    // The socket-level error passes through (not synthesized by us).
    CHECK(std::string(ev->error().category().name()) != "partake client");
}

TEST_CASE("client: malformed server hello reports connect_failed") {
    testing::tempdir const td;
    auto const path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    asio::io_context sctx; // Synchronous ops only; never run.
    asio::local::stream_protocol::endpoint const endpt(path.string());
    asio::local::stream_protocol::acceptor acceptor(sctx);
    acceptor.open(endpt.protocol());
    acceptor.bind(endpt);
    mock_server::unlinkable_type const unlk(path.string());
    acceptor.listen(1);

    client c;
    queue q;
    auto const id = c.connect(path.string(), "test", q, nullptr);
    asio::local::stream_protocol::socket peer(sctx);
    acceptor.accept(peer); // Blocks until the client connects.
    // Valid framing (aligned, prefix covers the frame), garbage payload.
    auto const garbage = std::vector<std::uint8_t>{
        12,   0,    0,    0,    0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    asio::write(peer, asio::buffer(garbage));

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error() == client_errc::connect_failed);
}

TEST_CASE("client: destruction right after connect submit delivers exactly "
          "one event") {
    testing::tempdir const td;
    auto const path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    asio::io_context sctx;
    asio::local::stream_protocol::endpoint const endpt(path.string());
    asio::local::stream_protocol::acceptor acceptor(sctx);
    acceptor.open(endpt.protocol());
    acceptor.bind(endpt);
    mock_server::unlinkable_type const unlk(path.string());
    acceptor.listen(1); // Backlog accepts the connection; never answered.

    queue q;
    op_id id = 0;
    {
        client c;
        id = c.connect(path.string(), "test", q, nullptr);
    } // ~client quits the in-flight connect; must not hang.
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::connect);
    CHECK(ev->error() == client_errc::connect_failed);
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());
}

TEST_CASE("client: undrained connect event does not leak the connection") {
    // Regression: an undrained connect-success event used to form an
    // ownership cycle (queue_impl -> event -> connection ->
    // connection_impl -> queue_impl) that outlived both public handles.
    std::weak_ptr<internal::queue_impl> wq;

    auto const connect_and_await_event = [](client &c, mock_server const &srv,
                                            queue &q) {
        (void)c.connect(srv.socket_path(), "test", q, nullptr);
        // The event (holding a connection handle) gets queued; never drain.
        struct pollfd pfd = {};
        pfd.fd = q.wakeup();
        pfd.events = POLLIN;
        REQUIRE(::poll(&pfd, 1, static_cast<int>(event_timeout.count())) == 1);
    };

    SECTION("queue dies before client") {
        mock_server const srv;
        client c;
        {
            queue q;
            wq = internal::get_queue_impl(q);
            connect_and_await_event(c, srv, q);
        } // ~queue discards the event.
    } // ~client tears down the connection.

    SECTION("client dies before queue") {
        mock_server const srv;
        std::optional<queue> q(std::in_place);
        wq = internal::get_queue_impl(*q);
        {
            client c;
            connect_and_await_event(c, srv, *q);
        } // ~client tears down; the event is still queued.
        q.reset(); // ~queue discards it.
    }

    CHECK(wq.expired());
}

TEST_CASE("client: dispatch returns despite a resubmitting completion on a "
          "stopped client") {
    // Regression (livelock): with the client stopped, a submit on a
    // surviving connection handle delivers the terminal event synchronously
    // on the submitting thread, so a completion that unconditionally
    // resubmits feeds the very queue being dispatched. dispatch() must
    // deliver only the events present at entry.
    mock_server const srv;
    queue q;
    connection conn;
    {
        client c;
        (void)c.connect(srv.socket_path(), "test", q, nullptr);
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        REQUIRE_FALSE(ev->error());
        conn = ev->get_connection();
        REQUIRE(conn);
    } // ~client stops the I/O thread; conn survives.

    int deliveries = 0;
    completion resubmit;
    resubmit = [&deliveries, &conn, &resubmit](event &&ev) {
        ++deliveries;
        CHECK(ev.error() == client_errc::disconnected);
        (void)conn.ping(resubmit);
    };
    (void)conn.ping(resubmit);

    q.dispatch();
    CHECK(deliveries == 1);

    // The follow-up event is pending, not delivered in the same call.
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->type() == op_type::ping);
    CHECK(ev->error() == client_errc::disconnected);
}

// NOLINTEND(readability-magic-numbers)

TEST_CASE("client: move transfers the impl") {
    client c;
    auto const *impl = internal::get_client_impl(c).get();
    REQUIRE(impl != nullptr);

    client c2(std::move(c));
    CHECK(internal::get_client_impl(c2).get() == impl);
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK(internal::get_client_impl(c) == nullptr);

    {
        // Destroying the moved-from client is safe.
        client const doomed(std::move(c)); // NOLINT(bugprone-use-after-move)
    }

    // The moved-to client still works.
    std::promise<void> prom;
    auto fut = prom.get_future();
    asio::post(internal::get_client_impl(c2)->context(),
               [&prom] { prom.set_value(); });
    fut.get();
}

} // namespace partake::client
