/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/connection.hpp"

#include "mock_server.hpp"
#include "partake/client.hpp"
#include "partake/errors.hpp"
#include "partake/event.hpp"
#include "partake/queue.hpp"
#include "partake/types.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <system_error>
#include <thread>
#include <utility>

#include <poll.h>

namespace partake::client {

TEST_CASE("connection: default-constructed is empty") {
    connection const conn;
    CHECK(not conn);

    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    connection const copy(conn);
    CHECK(not copy);
}

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("connection: copies share the impl") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);
    auto const copy = conn; // NOLINT(performance-unnecessary-copy-*)
    CHECK(internal::get_connection_impl(copy) ==
          internal::get_connection_impl(conn));
    CHECK(copy.connection_number() == conn.connection_number());
}

TEST_CASE("connection: ping round trip") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    SECTION("user_data overload") {
        int cookie = 0;
        auto const id = conn.ping(&cookie);
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        CHECK(ev->id() == id);
        CHECK(ev->type() == op_type::ping);
        CHECK_FALSE(ev->error());
        CHECK(ev->user_data() == &cookie);
        CHECK(srv.pings_received() == 1);
    }

    SECTION("completion overload, delivered via queue::dispatch") {
        std::optional<event> received;
        auto const id =
            conn.ping([&received](event &&ev) { received = std::move(ev); });
        struct pollfd pfd = {};
        pfd.fd = q.wakeup();
        pfd.events = POLLIN;
        REQUIRE(::poll(&pfd, 1, static_cast<int>(event_timeout.count())) == 1);
        q.dispatch();
        REQUIRE(received.has_value());
        CHECK(received->id() == id);
        CHECK(received->type() == op_type::ping);
        CHECK_FALSE(received->error());
    }
}

TEST_CASE("connection: concurrent pings each get exactly one event") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    std::set<op_id> ids;
    for (int i = 0; i < 10; ++i)
        ids.insert(conn.ping(nullptr));
    REQUIRE(ids.size() == 10);

    std::set<op_id> seen;
    for (int i = 0; i < 10; ++i) {
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        CHECK(ev->type() == op_type::ping);
        CHECK_FALSE(ev->error());
        CHECK_FALSE(seen.contains(ev->id()));
        seen.insert(ev->id());
    }
    CHECK(seen == ids);
    CHECK(srv.pings_received() == 10);
}

TEST_CASE("connection: canceled op still delivers exactly one event") {
    mock_server const srv({.respond_to_ping = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const ping_id = conn.ping(nullptr);
    conn.cancel(ping_id);
    auto const shutdown_id = conn.shutdown(nullptr);

    std::map<op_id, std::error_code> results;
    for (int i = 0; i < 2; ++i) {
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        CHECK_FALSE(results.contains(ev->id()));
        results[ev->id()] = ev->error();
    }
    CHECK(results.at(ping_id) == client_errc::canceled);
    CHECK_FALSE(results.at(shutdown_id));
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());
}

TEST_CASE("connection: cancel with unknown op_id is ignored") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    conn.cancel(999999);
    auto const id = conn.ping(nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK_FALSE(ev->error());
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());
}

TEST_CASE("connection: shutdown handshake completes after pending pings") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    std::set<op_id> ping_ids;
    for (int i = 0; i < 3; ++i)
        ping_ids.insert(conn.ping(nullptr));
    auto const shutdown_id = conn.shutdown(nullptr);

    for (int i = 0; i < 3; ++i) {
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        CHECK(ping_ids.count(ev->id()) == 1);
        CHECK_FALSE(ev->error());
    }
    auto ev = q.wait_one(event_timeout); // Shutdown completes last.
    REQUIRE(ev.has_value());
    CHECK(ev->id() == shutdown_id);
    CHECK(ev->type() == op_type::shutdown);
    CHECK_FALSE(ev->error());
    CHECK(srv.pings_received() == 3);
    CHECK(srv.quits_received() == 1);
}

TEST_CASE("connection: shutdown succeeds when the server closes without a "
          "QuitResponse") {
    mock_server const srv({.respond_to_quit = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.shutdown(nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::shutdown);
    CHECK_FALSE(ev->error());
    CHECK(srv.quits_received() == 1);
}

TEST_CASE("connection: submit after shutdown fails with disconnected") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    (void)conn.shutdown(nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK_FALSE(ev->error());

    auto const id = conn.ping(nullptr);
    ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::ping);
    CHECK(ev->error() == client_errc::disconnected);
}

TEST_CASE("connection: server EOF fails pending and subsequent ops") {
    mock_server srv({.respond_to_ping = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.ping(nullptr);
    spin_until([&srv] { return srv.pings_received() == 1; });
    srv.close_connection();

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error() == client_errc::disconnected);

    auto const id2 = conn.ping(nullptr);
    ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id2);
    CHECK(ev->error() == client_errc::disconnected);
}

TEST_CASE("connection: garbage response frame panics the connection") {
    mock_server srv({.respond_to_ping = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.ping(nullptr);
    spin_until([&srv] { return srv.pings_received() == 1; });
    // Valid framing (aligned, prefix covers the frame), garbage payload.
    srv.send_raw_frame({12, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff, 0xff});

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error() == client_errc::protocol_violation);

    auto const id2 = conn.ping(nullptr); // The connection is dead.
    ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id2);
    CHECK(ev->error() == client_errc::disconnected);
}

TEST_CASE("connection: response with unknown seqno panics the connection") {
    mock_server srv({.respond_to_ping = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.ping(nullptr);
    spin_until([&srv] { return srv.pings_received() == 1; });
    srv.send_response_to_unknown_seqno();

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error() == client_errc::protocol_violation);
}

TEST_CASE("connection: daemon-reported error status maps to protocol_errc") {
    mock_server srv({.respond_to_ping = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.ping(nullptr);
    spin_until([&srv] { return srv.pings_received() == 1; });
    srv.send_error_response(0, protocol::Status::OBJECT_BUSY);

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error() == protocol_errc::object_busy);
}

TEST_CASE("connection: ~client fails a pending op; no hang") {
    mock_server const srv({.respond_to_ping = false});
    queue q;
    op_id id = 0;
    {
        client c;
        auto conn = connect_or_fail(c, srv, q);
        id = conn.ping(nullptr);
        spin_until([&srv] { return srv.pings_received() == 1; });
    } // ~client quits the connection, failing the pending ping.
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::ping);
    CHECK(ev->error() == client_errc::disconnected);
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());
}

TEST_CASE("connection: move-assigning over a client quits it; no leak") {
    mock_server const srv({.respond_to_ping = false});
    queue q;
    client c;
    auto conn = connect_or_fail(c, srv, q);
    auto const id = conn.ping(nullptr);
    spin_until([&srv] { return srv.pings_received() == 1; });

    std::weak_ptr<internal::client_impl> const wclim(
        internal::get_client_impl(c));
    std::weak_ptr<internal::connection_impl> const wconn(
        internal::get_connection_impl(conn));

    c = client(); // Quits the assigned-over client, failing the ping.

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::ping);
    CHECK(ev->error() == client_errc::disconnected);
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());

    conn = connection(); // Drop the last reference to the dead connection.
    CHECK(wconn.expired());
    CHECK(wclim.expired());
}

TEST_CASE("connection: submit on a surviving handle after ~client") {
    mock_server const srv;
    queue q;
    connection conn;
    {
        client c;
        conn = connect_or_fail(c, srv, q);
    }
    // The submitting thread itself delivers the terminal event.
    auto const id = conn.ping(nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::ping);
    CHECK(ev->error() == client_errc::disconnected);
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::client
