/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/connection.hpp"

#include <catch2/catch_test_macros.hpp>

namespace partake::client {

TEST_CASE("connection: default-constructed is empty") {
    connection const conn;
    CHECK(not conn);

    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    connection const copy(conn);
    CHECK(not copy);
}

} // namespace partake::client
