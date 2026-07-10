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
    key_sequence seq(0xffff'ffff'ffff'ffffuLL);
    CHECK(~seq.generate().as_u64() != 0);
    CHECK(seq.generate().is_valid());
    CHECK(seq.generate() != seq.generate());
}

TEST_CASE("key_sequence: same seed reproduces sequence") {
    key_sequence seq1(0x1234'5678'9abc'def0uLL);
    key_sequence seq2(0x1234'5678'9abc'def0uLL);
    for (int i = 0; i < 5; ++i)
        CHECK(seq1.generate() == seq2.generate());
}

TEST_CASE("key_sequence: different seeds diverge") {
    key_sequence seq1(0x1234'5678'9abc'def0uLL);
    key_sequence seq2(0x0fed'cba9'8765'4321uLL);
    CHECK(seq1.generate() != seq2.generate());
}

TEST_CASE("key_sequence: tokens valid for arbitrary seeds") {
    for (auto seed : {1uLL, 42uLL, 0xdead'beef'dead'beefuLL}) {
        key_sequence seq(seed);
        CHECK(seq.generate().is_valid());
        CHECK(seq.generate().is_valid());
    }
}

} // namespace partake::daemon
