/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "errors.hpp"

#include <doctest.h>

#include <ostream> // operator<< used by doctest
#include <system_error>

TEST_CASE("errc") {
    std::error_code ec = partake::daemon::errc::message_too_long;
    CHECK(ec);
    CHECK(ec != std::make_error_code(static_cast<std::errc>(
                    partake::daemon::errc::message_too_long)));
    CHECK(ec == partake::daemon::errc::message_too_long);
    CHECK(ec != partake::daemon::errc::invalid_message);
    auto msg = ec.message();
    CHECK(msg.find("message") != msg.npos);

    std::error_code const ok = partake::daemon::errc(0);
    CHECK_FALSE(ok);
    CHECK(ok.message() == "Success");

    std::error_code const unk = partake::daemon::errc(-1);
    CHECK(unk);
    CHECK(unk.message() == "Unknown error");
}
