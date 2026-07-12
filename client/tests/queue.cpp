/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/queue.hpp"

#include "event_impl.hpp"
#include "partake/event.hpp"
#include "partake/types.hpp"
#include "queue_impl.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <poll.h>
#include <unistd.h>

namespace partake::client {

namespace {

auto make_test_event(op_id id, completion cont = {}, void *user_data = nullptr)
    -> event {
    internal::event_payload pl;
    pl.id = id;
    pl.type = op_type::ping;
    pl.user_data = user_data;
    pl.cont = std::move(cont);
    return internal::make_event(std::move(pl));
}

auto fd_readable(wakeup_handle fd) -> bool {
    struct pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    return ::poll(&pfd, 1, 0) == 1;
}

void push(queue const &q, event ev) {
    internal::get_queue_impl(q)->push(std::move(ev));
}

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("queue: fresh queue is empty and unsignaled") {
    queue q;
    CHECK(q.wakeup() >= 0);
    CHECK(not fd_readable(q.wakeup()));
    std::array<event, 4> out;
    CHECK(q.drain(out.data(), out.size()) == 0);
    CHECK(not q.wait_one(std::chrono::milliseconds(0)).has_value());
}

TEST_CASE("queue: push and drain a single event") {
    queue q;
    int marker = 0;
    push(q, make_test_event(42, {}, &marker));
    CHECK(fd_readable(q.wakeup()));

    std::array<event, 4> out;
    REQUIRE(q.drain(out.data(), out.size()) == 1);
    CHECK(out[0].id() == 42);
    CHECK(out[0].user_data() == &marker);
    CHECK(not fd_readable(q.wakeup()));
    CHECK(q.drain(out.data(), out.size()) == 0);
}

TEST_CASE("queue: wakeup bytes are coalesced") {
    queue q;
    push(q, make_test_event(1));
    push(q, make_test_event(2));
    push(q, make_test_event(3));

    // Exactly one byte is pending on the wakeup handle.
    std::array<char, 4> buf{};
    CHECK(::read(q.wakeup(), buf.data(), buf.size()) == 1);
    errno = 0;
    CHECK(::read(q.wakeup(), buf.data(), buf.size()) == -1);
    CHECK((errno == EAGAIN or errno == EWOULDBLOCK));

    // Drain is robust to the test having eaten the byte.
    std::array<event, 4> out;
    REQUIRE(q.drain(out.data(), out.size()) == 3);
    CHECK(out[0].id() == 1);
    CHECK(out[1].id() == 2);
    CHECK(out[2].id() == 3);
}

TEST_CASE("queue: partial drain retains the wakeup byte") {
    queue q;
    for (op_id i = 1; i <= 5; ++i)
        push(q, make_test_event(i));

    std::array<event, 2> out;
    CHECK(q.drain(out.data(), 2) == 2);
    CHECK(fd_readable(q.wakeup()));
    CHECK(q.drain(out.data(), 2) == 2);
    CHECK(fd_readable(q.wakeup()));
    CHECK(q.drain(out.data(), 2) == 1);
    CHECK(not fd_readable(q.wakeup()));
}

TEST_CASE("queue: re-signals on empty-to-nonempty transition") {
    queue q;
    push(q, make_test_event(1));
    std::array<event, 4> out;
    REQUIRE(q.drain(out.data(), out.size()) == 1);
    CHECK(not fd_readable(q.wakeup()));

    push(q, make_test_event(2));
    CHECK(fd_readable(q.wakeup()));
}

TEST_CASE("queue: wait_one returns immediately when nonempty") {
    queue q;
    push(q, make_test_event(7));
    auto ev = q.wait_one(std::chrono::milliseconds(-1));
    REQUIRE(ev.has_value());
    CHECK(ev->id() == 7);
}

TEST_CASE("queue: wait_one times out on an empty queue") {
    queue q;
    auto const start = std::chrono::steady_clock::now();
    auto ev = q.wait_one(std::chrono::milliseconds(50));
    auto const elapsed = std::chrono::steady_clock::now() - start;
    CHECK(not ev.has_value());
    CHECK(elapsed >= std::chrono::milliseconds(40));
}

TEST_CASE("queue: wait_one wakes up on a push from another thread") {
    queue q;
    std::thread pusher([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        push(q, make_test_event(11));
    });
    // Finite timeout so a regression fails instead of hanging.
    auto ev = q.wait_one(std::chrono::milliseconds(5000));
    pusher.join();
    REQUIRE(ev.has_value());
    CHECK(ev->id() == 11);
}

TEST_CASE("queue: wait_one leaves a remainder signaled") {
    queue q;
    push(q, make_test_event(1));
    push(q, make_test_event(2));

    auto ev = q.wait_one(std::chrono::milliseconds(0));
    REQUIRE(ev.has_value());
    CHECK(ev->id() == 1);
    CHECK(fd_readable(q.wakeup()));

    auto ev2 = q.wait_one(std::chrono::milliseconds(0));
    REQUIRE(ev2.has_value());
    CHECK(ev2->id() == 2);
    CHECK(not fd_readable(q.wakeup()));
}

TEST_CASE("queue: dispatch routes continuations and fallback") {
    queue q;
    std::vector<op_id> delivered;
    auto record = [&delivered](event &&ev) { delivered.push_back(ev.id()); };
    int marker = 0;
    push(q, make_test_event(1, record));      // A
    push(q, make_test_event(2, {}, &marker)); // B
    push(q, make_test_event(3, record));      // C

    std::vector<op_id> fell_back;
    q.dispatch([&fell_back](event &&ev) { fell_back.push_back(ev.id()); });
    CHECK(delivered == std::vector<op_id>{1, 3});
    CHECK(fell_back == std::vector<op_id>{2});
    std::array<event, 4> out;
    CHECK(q.drain(out.data(), out.size()) == 0);
    CHECK(not fd_readable(q.wakeup()));

    // A second dispatch runs nothing.
    delivered.clear();
    fell_back.clear();
    q.dispatch([&fell_back](event &&ev) { fell_back.push_back(ev.id()); });
    CHECK(delivered.empty());
    CHECK(fell_back.empty());
}

TEST_CASE("queue: dispatch without fallback drops user_data-only events") {
    queue q;
    int marker = 0;
    push(q, make_test_event(1, {}, &marker));
    q.dispatch();
    std::array<event, 4> out;
    CHECK(q.drain(out.data(), out.size()) == 0);
    CHECK(not fd_readable(q.wakeup()));
}

TEST_CASE("queue: racing pushes from multiple threads") {
    static constexpr std::size_t n_threads = 4;
    static constexpr op_id per_thread = 250;
    queue q;
    std::vector<std::thread> pushers;
    pushers.reserve(n_threads);
    for (std::size_t t = 0; t < n_threads; ++t) {
        pushers.emplace_back([&q, t] {
            for (op_id i = 0; i < per_thread; ++i)
                push(q, make_test_event(t * per_thread + i + 1));
        });
    }

    std::multiset<op_id> ids;
    std::array<event, 16> out;
    while (ids.size() < n_threads * per_thread) {
        auto ev = q.wait_one(std::chrono::milliseconds(5000));
        REQUIRE(ev.has_value());
        ids.insert(ev->id());
        auto const n = q.drain(out.data(), out.size());
        for (std::size_t i = 0; i < n; ++i)
            ids.insert(out[i].id());
    }
    for (auto &th : pushers)
        th.join();

    REQUIRE(ids.size() == n_threads * per_thread);
    for (op_id i = 1; i <= n_threads * per_thread; ++i)
        CHECK(ids.count(i) == 1);
    CHECK(not fd_readable(q.wakeup()));
}

TEST_CASE("queue: move transfers the shared impl") {
    queue q;
    auto const wk = q.wakeup();
    push(q, make_test_event(9));

    queue q2(std::move(q));
    CHECK(q2.wakeup() == wk);
    CHECK(internal::get_queue_impl(q2) != nullptr);
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK(internal::get_queue_impl(q) == nullptr);

    std::array<event, 4> out;
    REQUIRE(q2.drain(out.data(), out.size()) == 1);
    CHECK(out[0].id() == 9);
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::client
