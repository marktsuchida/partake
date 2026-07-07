/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "errors.hpp"

#include <doctest.h>

#include <ostream> // operator<< used by doctest
#include <system_error>

TEST_CASE("errc") {
    std::error_code ec = partake::common::errc::message_too_long;
    CHECK(ec);
    CHECK(ec != std::make_error_code(static_cast<std::errc>(
                    partake::common::errc::message_too_long)));
    CHECK(ec == partake::common::errc::message_too_long);
    CHECK(ec != partake::common::errc::invalid_message);
    CHECK(ec.message().find("message") != std::string::npos);

    std::error_code const ok = partake::common::errc(0);
    CHECK_FALSE(ok);
    CHECK(ok.message() == "Success");

    std::error_code const unk = partake::common::errc(-1);
    CHECK(unk);
    CHECK(unk.message().find("Unknown error") == 0);
}

TEST_CASE("protocol_errc") {
    std::error_code ec = partake::common::protocol_errc::invalid_request;
    CHECK(ec);
    CHECK(ec != std::make_error_code(static_cast<std::errc>(
                    partake::common::protocol_errc::invalid_request)));
    CHECK(ec == partake::common::protocol_errc::invalid_request);
    CHECK(ec != partake::common::protocol_errc::no_such_object);
    CHECK(ec.message().find("request") != std::string::npos);

    std::error_code const ok = partake::common::protocol_errc(0);
    CHECK_FALSE(ok);
    CHECK(ok.message() == "Success");

    std::error_code const unk = partake::common::protocol_errc(-42);
    CHECK(unk);
    CHECK(unk.message().find("Unknown error") == 0);
}
