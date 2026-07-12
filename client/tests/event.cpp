/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/event.hpp"

#include "event_impl.hpp"
#include "partake/errors.hpp"
#include "partake/token.hpp"
#include "partake/types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <utility>

namespace partake::client {

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("event: empty event is benignly queryable") {
    event ev;
    CHECK(ev.id() == 0);
    CHECK(ev.type() == op_type::none);
    CHECK(ev.error() == std::error_code());
    CHECK(ev.user_data() == nullptr);
    CHECK(not ev.get_connection());
    CHECK(not ev.object());
    CHECK(not ev.key().is_valid());
    CHECK(not ev.zeroed());
    CHECK(not ev.deliver());
}

TEST_CASE("event: reflects its payload") {
    int marker = 0;
    internal::event_payload pl;
    pl.id = 42;
    pl.type = op_type::alloc;
    pl.error = client_errc::canceled;
    pl.user_data = &marker;
    pl.zeroed = true;
    auto ev = internal::make_event(std::move(pl));
    CHECK(ev.id() == 42);
    CHECK(ev.type() == op_type::alloc);
    CHECK(ev.error() == client_errc::canceled);
    CHECK(ev.user_data() == &marker);
    CHECK(ev.zeroed());
    CHECK(not ev.deliver()); // No continuation attached.
}

TEST_CASE("event: continuation is one-shot across sharing copies") {
    int count = 0;
    op_id seen = 0;
    internal::event_payload pl;
    pl.id = 5;
    pl.cont = [&](event &&e) {
        ++count;
        seen = e.id();
    };
    auto ev = internal::make_event(std::move(pl));
    event copy(ev);

    SECTION("deliver original first") {
        CHECK(ev.deliver());
        CHECK(not copy.deliver());
    }

    SECTION("deliver copy first") {
        CHECK(copy.deliver());
        CHECK(not ev.deliver());
    }

    CHECK(count == 1);
    CHECK(seen == 5);
}

TEST_CASE("event: continuation may drop its argument") {
    internal::event_payload pl;
    pl.id = 6;
    pl.cont = [](event &&e) { event const sink(std::move(e)); };
    auto ev = internal::make_event(std::move(pl));
    CHECK(ev.deliver());
    // The caller's handle remains valid.
    CHECK(ev.id() == 6);
    CHECK(not ev.deliver()); // Consumed.
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::client
