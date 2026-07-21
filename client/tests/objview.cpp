/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/objview.hpp"

#include "mock_server.hpp"
#include "partake/client.hpp"
#include "partake/connection.hpp"
#include "partake/errors.hpp"
#include "partake/event.hpp"
#include "partake/queue.hpp"
#include "partake/token.hpp"
#include "partake/types.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <set>

namespace partake::client {

TEST_CASE("objview: default-constructed is empty") {
    objview const obj;
    CHECK(not obj);

    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    objview const copy(obj);
    CHECK(not copy);
}

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Alloc and return the objview from the completion event (which is dropped,
// leaving the returned handle as the only one).
auto alloc_or_fail(connection &conn, queue &q, std::uint64_t size) -> objview {
    (void)conn.alloc(size, {}, nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    REQUIRE_FALSE(ev->error());
    auto obj = ev->object();
    REQUIRE(obj);
    return obj;
}

} // namespace

TEST_CASE("objview: alloc round trip") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    int cookie = 0;
    auto const id = conn.alloc(64, {}, &cookie);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::alloc);
    REQUIRE_FALSE(ev->error());
    CHECK(ev->user_data() == &cookie);
    CHECK_FALSE(ev->zeroed());
    auto obj = ev->object();
    REQUIRE(obj);
    CHECK(obj.key().is_valid());
    CHECK(obj.size() == 64);
    CHECK(obj.writable());
    REQUIRE(obj.data() != nullptr);

    // Writes through data() land in the server's segment (same shared
    // memory; the first alloc is at offset 0).
    std::memset(obj.data(), 0x5a, 64);
    auto const *seg =
        static_cast<unsigned char const *>(srv.segment_address());
    CHECK(seg[0] == 0x5a);
    CHECK(seg[63] == 0x5a);
    CHECK(srv.allocs_received() == 1);
}

TEST_CASE("objview: alloc reports zeroed when the daemon says so") {
    mock_server const srv({.alloc_zeroed = true});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    (void)conn.alloc(16, {}, nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    REQUIRE_FALSE(ev->error());
    CHECK(ev->zeroed());
}

TEST_CASE("objview: open") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);
    auto alloced = alloc_or_fail(conn, q, 32);
    auto const key = alloced.key();

    SECTION("default policy: read-only view of the same bytes") {
        std::memset(alloced.data(), 0x77, 32);
        auto const id = conn.open(key, {}, nullptr);
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        CHECK(ev->id() == id);
        CHECK(ev->type() == op_type::open);
        REQUIRE_FALSE(ev->error());
        auto obj = ev->object();
        REQUIRE(obj);
        CHECK(obj.key() == key);
        CHECK(obj.size() == 32);
        CHECK_FALSE(obj.writable());
        REQUIRE(obj.data() != nullptr);
        CHECK(obj.data() == alloced.data());
        CHECK(static_cast<unsigned char const *>(obj.data())[0] == 0x77);
    }

    SECTION("primitive policy: writable") {
        auto const id = conn.open(key, {.pol = policy::primitive}, nullptr);
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        CHECK(ev->id() == id);
        REQUIRE_FALSE(ev->error());
        auto obj = ev->object();
        REQUIRE(obj);
        CHECK(obj.writable());
    }
}

TEST_CASE("objview: deferred open completes when the response arrives") {
    mock_server srv({.respond_to_open = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);
    auto alloced = alloc_or_fail(conn, q, 16);

    auto const id = conn.open(alloced.key(), {}, nullptr);
    spin_until([&srv] { return srv.opens_received() == 1; });
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());

    srv.release_withheld_open_responses();
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::open);
    REQUIRE_FALSE(ev->error());
    CHECK(ev->object());
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());
}

TEST_CASE("objview: explicit close") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);
    auto obj = alloc_or_fail(conn, q, 16);

    auto const id = obj.close(nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::close);
    CHECK_FALSE(ev->error());
    CHECK(srv.closes_received() == 1);
    CHECK(obj.data() == nullptr);

    // A second close does not reach the wire and reports object_closed.
    auto const id2 = obj.close(nullptr);
    ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id2);
    CHECK(ev->type() == op_type::close);
    CHECK(ev->error() == client_errc::object_closed);
    CHECK(srv.closes_received() == 1);
}

TEST_CASE("objview: dropping the last handle auto-closes, with no event") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    (void)conn.alloc(16, {}, nullptr);
    {
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        REQUIRE_FALSE(ev->error());
        REQUIRE(ev->object());
    } // Drops the event, and with it the only objview handle.
    spin_until([&srv] { return srv.closes_received() == 1; });
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());
}

TEST_CASE("objview: cancel auto-closes an abandoned alloc") {
    mock_server srv({.respond_to_alloc = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.alloc(16, {}, nullptr);
    spin_until([&srv] { return srv.allocs_received() == 1; });
    conn.cancel(id);
    srv.release_withheld_alloc_responses();

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::alloc);
    CHECK(ev->error() == client_errc::canceled);
    CHECK_FALSE(ev->object());
    spin_until([&srv] { return srv.closes_received() == 1; });
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());
}

TEST_CASE("objview: alloc unwind-closes when the segment fetch fails") {
    mock_server srv({.respond_to_get_segment = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.alloc(16, {}, nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::alloc);
    CHECK(ev->error() == protocol_errc::no_such_segment);
    CHECK_FALSE(ev->object());
    spin_until([&srv] { return srv.closes_received() == 1; });
    CHECK_FALSE(q.wait_one(std::chrono::milliseconds(100)).has_value());
}

TEST_CASE("objview: daemon alloc error maps to protocol_errc") {
    mock_server srv({.respond_to_alloc = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.alloc(16, {}, nullptr);
    spin_until([&srv] { return srv.allocs_received() == 1; });
    srv.send_error_response(0, protocol::Status::OUT_OF_SHMEM);

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error() == protocol_errc::out_of_shmem);
    CHECK_FALSE(ev->object());
    CHECK(srv.closes_received() == 0); // Nothing to unwind.
}

TEST_CASE("objview: open of an unknown key reports no_such_object") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    auto const id = conn.open(token(12345), {}, nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->type() == op_type::open);
    CHECK(ev->error() == protocol_errc::no_such_object);
    CHECK_FALSE(ev->object());
}

TEST_CASE("objview: daemon open error maps to protocol_errc") {
    mock_server srv({.respond_to_open = false});
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);
    auto alloced = alloc_or_fail(conn, q, 16);

    auto const id = conn.open(alloced.key(), {}, nullptr);
    spin_until([&srv] { return srv.opens_received() == 1; });
    // Seqnos: 0 = alloc, 1 = get segment, 2 = this open.
    srv.send_error_response(2, protocol::Status::OBJECT_BUSY);

    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == id);
    CHECK(ev->error() == protocol_errc::object_busy);
    CHECK_FALSE(ev->object());
}

TEST_CASE("objview: concurrent allocs each get exactly one event") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);

    std::set<op_id> ids;
    for (int i = 0; i < 10; ++i)
        ids.insert(conn.alloc(8, {}, nullptr));
    REQUIRE(ids.size() == 10);

    std::set<op_id> seen;
    for (int i = 0; i < 10; ++i) {
        auto ev = q.wait_one(event_timeout);
        REQUIRE(ev.has_value());
        CHECK(ev->type() == op_type::alloc);
        CHECK_FALSE(ev->error());
        REQUIRE(ev->object());
        CHECK_FALSE(seen.contains(ev->id()));
        seen.insert(ev->id());
    }
    CHECK(seen == ids);
    CHECK(srv.allocs_received() == 10);
}

TEST_CASE("objview: submits after shutdown fail with disconnected") {
    mock_server const srv;
    client c;
    queue q;
    auto conn = connect_or_fail(c, srv, q);
    auto obj = alloc_or_fail(conn, q, 16);

    (void)conn.shutdown(nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK_FALSE(ev->error());

    auto const alloc_id = conn.alloc(8, {}, nullptr);
    ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == alloc_id);
    CHECK(ev->type() == op_type::alloc);
    CHECK(ev->error() == client_errc::disconnected);

    auto const close_id = obj.close(nullptr);
    ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    CHECK(ev->id() == close_id);
    CHECK(ev->type() == op_type::close);
    CHECK(ev->error() == client_errc::disconnected);
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::client
