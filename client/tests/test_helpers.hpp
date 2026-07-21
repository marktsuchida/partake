/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "partake/client.hpp"
#include "partake/connection.hpp"
#include "partake/queue.hpp"

#include "mock_server.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

namespace partake::client {

inline constexpr auto event_timeout = std::chrono::milliseconds(5000);

inline auto connect_or_fail(client &c, mock_server const &srv, queue &q)
    -> connection {
    (void)c.connect(srv.socket_path(), "test", q, nullptr);
    auto ev = q.wait_one(event_timeout);
    REQUIRE(ev.has_value());
    REQUIRE_FALSE(ev->error());
    auto conn = ev->get_connection();
    REQUIRE(conn);
    return conn;
}

// Wait for a server-side condition (e.g. a withheld ping's arrival) so a
// subsequent scripted server action happens with the request pending.
template <typename Cond> void spin_until(Cond cond) {
    auto const deadline = std::chrono::steady_clock::now() + event_timeout;
    while (not cond()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace partake::client
