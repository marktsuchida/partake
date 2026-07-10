/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "client.hpp"

#include "asio.hpp"
#include "message.hpp"
#include "posix.hpp"
#include "testing.hpp"
#include "win32.hpp"

#include <catch2/catch_test_macros.hpp>

#include <flatbuffers/flatbuffers.h>
#include <gsl/span>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace partake::daemon {

namespace {

// Shared hook between a test and the fakes inside the client under test.
// Passed to the client as its "segment" so that the fake session (and via
// the session, the fake request handler) can reach it.
struct test_state {
    std::function<void(std::vector<std::uint8_t> &&)> write_message;
    std::function<auto(gsl::span<std::uint8_t const>)->bool> handle_message =
        [](gsl::span<std::uint8_t const>) { return false; };
};

struct fake_session {
    test_state *state;

    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    explicit fake_session(std::uint32_t session_id, test_state &st,
                          int &allocator, int &repository,
                          std::chrono::milliseconds voucher_ttl)
        : state(&st) {
        (void)session_id;
        (void)allocator;
        (void)repository;
        (void)voucher_ttl;
    }
    // NOLINTEND(bugprone-easily-swappable-parameters)

    [[nodiscard]] static auto session_id() -> std::uint32_t { return 0; }
    [[nodiscard]] static auto pid() -> std::uint32_t { return 0; }
    [[nodiscard]] static auto name() -> std::string { return "fake"; }
    void drop_pending_requests() {}
};

struct fake_request_handler {
    test_state *state;

    template <typename WriteFunc, typename HousekeepFunc, typename ErrorFunc>
    explicit fake_request_handler(fake_session &sess, WriteFunc write_message,
                                  HousekeepFunc housekeep, ErrorFunc error)
        : state(sess.state) {
        state->write_message = std::move(write_message);
        (void)housekeep;
        (void)error;
    }

    [[nodiscard]] auto
    handle_message(gsl::span<std::uint8_t const> bytes) const -> bool {
        return state->handle_message(bytes);
    }
};

using socket_type = asio::local::stream_protocol::socket;
using reader_type = common::async_message_reader<socket_type>;
using writer_type =
    common::async_message_writer<socket_type, std::vector<std::uint8_t>>;
using client_type = client<socket_type, reader_type, writer_type, fake_session,
                           fake_request_handler>;

#ifdef _WIN32
using unlinkable_type = common::win32::unlinkable;
#else
using unlinkable_type = common::posix::unlinkable;
#endif

// A client under test connected (via Unix domain socket) to a peer socket
// that plays the remote client program.
struct client_fixture {
    testing::tempdir td;
    asio::io_context ctx;
    socket_type peer{ctx};

    test_state state;
    int dummy_allocator = 0;
    int dummy_repository = 0;

    std::shared_ptr<client_type> owner; // Emulates the daemon's clients set.
    std::weak_ptr<client_type> observer;
    unsigned close_count = 0;

    client_fixture() {
        auto path = testing::unique_path(
            td.path(), testing::make_test_filename(__FILE__, __LINE__));
        asio::local::stream_protocol::endpoint const endpt(path.string());
        asio::local::stream_protocol::acceptor server(ctx);
        server.open(endpt.protocol());
        server.bind(endpt);
        unlinkable_type const unlk(path.string());
        server.listen(socket_type::max_listen_connections);
        peer.connect(endpt);
        socket_type served(ctx);
        server.accept(served);

        owner = std::make_shared<client_type>(
            std::move(served), 0, state, dummy_allocator, dummy_repository,
            std::chrono::seconds(1), [] {},
            [this](client_type &) {
                ++close_count;
                owner.reset();
            });
        // Cannot be a member initializer: 'owner' is created just above,
        // in the constructor body.
        // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
        observer = owner;
        owner->start();
    }
};

auto make_frame(std::size_t frame_size) -> std::vector<std::uint8_t> {
    assert(frame_size % common::message_frame_alignment == 0);
    std::vector<std::uint8_t> frame(frame_size);
    auto const prefix = static_cast<std::uint32_t>(
        frame_size - sizeof(flatbuffers::uoffset_t));
    flatbuffers::WriteScalar(frame.data(), prefix);
    return frame;
}

} // namespace

TEST_CASE("client: destroyed after normal disconnect") {
    client_fixture fx;

    fx.peer.close(); // Client sees EOF.
    fx.ctx.run();

    CHECK(fx.close_count == 1);
    CHECK(fx.owner == nullptr);
    CHECK(fx.observer.expired());
}

TEST_CASE("client: request/response round trip, then disconnect") {
    // NOLINTBEGIN(readability-magic-numbers)
    client_fixture fx;

    unsigned requests_received = 0;
    std::vector<std::uint8_t> const response{4, 0, 0, 0, 'r', 's', 'p', '\0'};
    fx.state.handle_message = [&](gsl::span<std::uint8_t const> bytes) {
        CHECK(bytes.size() == 8);
        ++requests_received;
        fx.state.write_message(std::vector<std::uint8_t>(response));
        return false;
    };

    std::vector<std::uint8_t> const request{4, 0, 0, 0, 0, 0, 0, 0};
    asio::write(fx.peer, asio::buffer(request));

    std::vector<std::uint8_t> received(8);
    asio::async_read(fx.peer, asio::buffer(received),
                     [&](boost::system::error_code ec, std::size_t nread) {
                         CHECK_FALSE(ec);
                         CHECK(nread == 8);
                         fx.peer.close();
                     });
    fx.ctx.run();

    CHECK(requests_received == 1);
    CHECK(received == response);
    CHECK(fx.close_count == 1);
    CHECK(fx.observer.expired());
    // NOLINTEND(readability-magic-numbers)
}

TEST_CASE("client: quit with read in flight") {
    // Regression test: shutting down with the client's read pending must
    // defer destruction until the aborted handler has drained (previously a
    // use-after-free).
    client_fixture fx;

    fx.ctx.poll(); // Ensure the initial read is pending.

    // Simulate partake_daemon::quit().
    fx.owner->prepare_for_shutdown();
    fx.owner->close();
    fx.owner.reset();

    // The pending read's handler still holds a keepalive.
    CHECK_FALSE(fx.observer.expired());

    fx.ctx.run();
    CHECK(fx.close_count == 1);
    CHECK(fx.observer.expired());
}

TEST_CASE("client: quit with write in flight") {
    client_fixture fx;

    // Inject writes well beyond the socket buffer size, without the peer
    // reading, so that they cannot complete before shutdown.
    for (int i = 0; i < 8; ++i)
        fx.state.write_message(make_frame(common::max_message_frame_len));

    fx.owner->prepare_for_shutdown();
    fx.owner->close();
    fx.owner.reset();

    CHECK_FALSE(fx.observer.expired());

    fx.ctx.run();
    CHECK(fx.close_count == 1);
    CHECK(fx.observer.expired());
}

} // namespace partake::daemon
