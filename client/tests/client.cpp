/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/client.hpp"

#include "asio.hpp"
#include "client_impl.hpp"
#include "partake/types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <future>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace partake::client {

TEST_CASE("client: constructs and destructs without hanging") {
    SECTION("default constructor") { client const c; }

    SECTION("options constructor") { client const c((client_options())); }
}

TEST_CASE("client: posted tasks run on the I/O thread") {
    client const c;
    std::promise<std::thread::id> prom;
    auto fut = prom.get_future();
    asio::post(internal::get_client_impl(c)->context(),
               [&prom] { prom.set_value(std::this_thread::get_id()); });
    CHECK(fut.get() != std::this_thread::get_id());
}

TEST_CASE("client: op_id counter starts at 1 and increases") {
    client const c;
    auto const &impl = internal::get_client_impl(c);
    CHECK(impl->next_op_id() == 1);
    CHECK(impl->next_op_id() == 2);
    CHECK(impl->next_op_id() == 3);
}

TEST_CASE("client: op_ids are unique across threads") {
    static constexpr std::size_t n_threads = 4;
    static constexpr std::size_t per_thread = 1000;
    client const c;
    auto const &impl = internal::get_client_impl(c);

    std::vector<std::vector<op_id>> ids(n_threads);
    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (std::size_t t = 0; t < n_threads; ++t) {
        threads.emplace_back([&impl, &ids, t] {
            ids[t].reserve(per_thread);
            for (std::size_t i = 0; i < per_thread; ++i)
                ids[t].push_back(impl->next_op_id());
        });
    }
    for (auto &th : threads)
        th.join();

    std::set<op_id> unique;
    for (auto const &v : ids)
        unique.insert(v.begin(), v.end());
    CHECK(unique.size() == n_threads * per_thread);
    CHECK(unique.count(0) == 0);
}

TEST_CASE("client: move transfers the impl") {
    client c;
    auto const *impl = internal::get_client_impl(c).get();
    REQUIRE(impl != nullptr);

    client c2(std::move(c));
    CHECK(internal::get_client_impl(c2).get() == impl);
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK(internal::get_client_impl(c) == nullptr);

    {
        // Destroying the moved-from client is safe.
        client const doomed(std::move(c)); // NOLINT(bugprone-use-after-move)
    }

    // The moved-to client still works.
    std::promise<void> prom;
    auto fut = prom.get_future();
    asio::post(internal::get_client_impl(c2)->context(),
               [&prom] { prom.set_value(); });
    fut.get();
}

} // namespace partake::client
