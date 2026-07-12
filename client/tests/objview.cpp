/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/objview.hpp"

#include <catch2/catch_test_macros.hpp>

namespace partake::client {

TEST_CASE("objview: default-constructed is empty") {
    objview const obj;
    CHECK(not obj);

    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    objview const copy(obj);
    CHECK(not copy);
}

} // namespace partake::client
