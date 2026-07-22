/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "unique_handler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

namespace partake::client::internal {

TEST_CASE("unique_handler: default-constructed is falsy") {
    unique_handler<void()> const h;
    CHECK_FALSE(h);
}

TEST_CASE("unique_handler: stores a move-only callable") {
    auto p = std::make_unique<int>(42);
    unique_handler<int()> h([p = std::move(p)] { return *p; });
    REQUIRE(h);
    CHECK(h() == 42);
}

TEST_CASE("unique_handler: invocation consumes; slot is reassignable") {
    int calls = 0;
    unique_handler<void()> h([&calls] { ++calls; });
    h();
    CHECK(calls == 1);
    CHECK_FALSE(h);

    h = unique_handler<void()>([&calls] { calls += 10; });
    REQUIRE(h);
    h();
    CHECK(calls == 11);
    CHECK_FALSE(h);
}

TEST_CASE("unique_handler: move transfers the callable") {
    unique_handler<int()> h([] { return 7; });
    unique_handler<int()> h2(std::move(h));
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK_FALSE(h);
    REQUIRE(h2);
    CHECK(h2() == 7);
}

TEST_CASE("unique_handler: arguments and return value propagate") {
    unique_handler<int(int, int)> h([](int a, int b) { return a + b; });
    CHECK(h(3, 4) == 7);
}

} // namespace partake::client::internal
