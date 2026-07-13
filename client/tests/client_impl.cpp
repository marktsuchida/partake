/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "client_impl.hpp"

#include <catch2/catch_test_macros.hpp>

#include <future>

namespace partake::client::internal {

TEST_CASE("client_impl: try_post runs the task; rejects after quit") {
    client_impl impl;

    std::promise<void> prom;
    auto fut = prom.get_future();
    CHECK(impl.try_post([&prom] { prom.set_value(); }));
    fut.get();

    impl.quit();
    bool ran = false;
    CHECK_FALSE(impl.try_post([&ran] { ran = true; }));
    CHECK_FALSE(ran);
}

TEST_CASE("client_impl: quit is idempotent") {
    client_impl impl;
    impl.quit();
    impl.quit();
} // ~client_impl calls quit() again as a safety net.

} // namespace partake::client::internal
