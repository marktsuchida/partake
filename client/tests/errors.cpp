/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/errors.hpp"

// client/src has no errors.hpp, so this resolves to common's.
#include "errors.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <system_error>
#include <vector>

namespace partake::client {

TEST_CASE("errors: category names") {
    CHECK(std::string(protocol_category().name()) == "partake protocol");
    CHECK(std::string(client_category().name()) == "partake client");
}

TEST_CASE("errors: protocol messages delegate to the common category") {
    for (int v = 1; v <= 6; ++v) {
        CAPTURE(v);
        auto const pub = make_error_code(protocol_errc(v));
        auto const com = common::make_error_code(common::protocol_errc(v));
        CHECK(pub.message() == com.message());
        CHECK(pub.message() != "Unknown error (partake protocol)");
    }
    CHECK(
        make_error_code(protocol_errc::unknown_protocol_error).message() ==
        common::make_error_code(common::protocol_errc::unknown_protocol_error)
            .message());
}

TEST_CASE("errors: client messages") {
    std::vector<client_errc> const all = {
        client_errc::canceled,
        client_errc::disconnected,
        client_errc::connect_failed,
        client_errc::unsupported_protocol_version,
        client_errc::protocol_violation,
        client_errc::object_closed,
    };
    for (auto const e : all) {
        CAPTURE(static_cast<int>(e));
        auto const msg = make_error_code(e).message();
        CHECK(not msg.empty());
        CHECK(msg != "Unknown error (partake client)");
    }
}

TEST_CASE("errors: error_code interoperation") {
    std::error_code const ec = client_errc::canceled;
    CHECK(ec == client_errc::canceled);
    CHECK(ec != client_errc::disconnected);
    CHECK(ec.value() == static_cast<int>(client_errc::canceled));
    CHECK(&ec.category() == &client_category());

    std::error_code const pec = protocol_errc::object_busy;
    CHECK(pec == protocol_errc::object_busy);
    CHECK(pec.value() == static_cast<int>(protocol_errc::object_busy));

    CHECK(&protocol_category() != &client_category());
    CHECK(&protocol_category() != &std::generic_category());
    CHECK(&client_category() != &std::generic_category());

    CHECK(not std::error_code());
}

} // namespace partake::client
