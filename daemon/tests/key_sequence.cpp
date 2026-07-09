/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "key_sequence.hpp"

#include <catch2/catch_test_macros.hpp>

namespace partake::daemon {

TEST_CASE("key_sequence") {
    // Smoke test only.
    key_sequence seq;
    CHECK(~seq.generate().as_u64() != 0);
    CHECK(seq.generate().is_valid());
    CHECK(seq.generate() != seq.generate());
}

} // namespace partake::daemon
