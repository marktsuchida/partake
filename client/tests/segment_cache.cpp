/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "segment_cache.hpp"

#include "mapping.hpp"
#include "requests.hpp"
#include "shmem_mmap.hpp"

#include "asio.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <system_error>

namespace partake::client::internal {

namespace {

constexpr std::size_t page = 16384;

auto spec_for(daemon::mmap_shmem const &seg) -> segment_spec {
    return segment_spec{seg.size(), posix_mmap_spec{seg.name(), true}};
}

} // namespace

TEST_CASE("segment_cache: fetches, attaches, and caches") {
    asio::io_context ctx;
    auto seg = daemon::create_posix_mmap_shmem(page);
    REQUIRE(seg.is_valid());

    int fetch_count = 0;
    segment_cache cache([&](std::uint32_t) -> asio::awaitable<segment_spec> {
        ++fetch_count;
        co_return spec_for(seg);
    });

    std::shared_ptr<mapping> got1;
    std::shared_ptr<mapping> got2;
    std::exception_ptr err;
    asio::co_spawn(
        ctx,
        [&]() -> asio::awaitable<void> {
            got1 = co_await cache.get(0);
            got2 = co_await cache.get(0);
        },
        [&](std::exception_ptr e) { err = e; });
    ctx.run();

    REQUIRE_FALSE(err);
    REQUIRE(got1 != nullptr);
    CHECK(got1 == got2);     // Same shared_ptr on the second get.
    CHECK(fetch_count == 1); // Not re-fetched.
}

TEST_CASE("segment_cache: coalesces concurrent gets") {
    asio::io_context ctx;
    auto seg = daemon::create_posix_mmap_shmem(page);
    REQUIRE(seg.is_valid());

    int fetch_count = 0;
    segment_cache cache([&](std::uint32_t) -> asio::awaitable<segment_spec> {
        ++fetch_count;
        // Suspend so a second caller observes the entry as pending and parks.
        co_await asio::post(ctx, asio::use_awaitable);
        co_return spec_for(seg);
    });

    std::shared_ptr<mapping> a;
    std::shared_ptr<mapping> b;
    asio::co_spawn(
        ctx, [&]() -> asio::awaitable<void> { a = co_await cache.get(0); },
        asio::detached);
    asio::co_spawn(
        ctx, [&]() -> asio::awaitable<void> { b = co_await cache.get(0); },
        asio::detached);
    ctx.run();

    REQUIRE(a != nullptr);
    CHECK(a == b);           // Both callers got the one mapping.
    CHECK(fetch_count == 1); // A single fetch served both.
}

TEST_CASE("segment_cache: failure propagates and leaves the entry retryable") {
    asio::io_context ctx;
    auto seg = daemon::create_posix_mmap_shmem(page);
    REQUIRE(seg.is_valid());

    int fetch_count = 0;
    segment_cache cache([&](std::uint32_t) -> asio::awaitable<segment_spec> {
        ++fetch_count;
        if (fetch_count == 1) {
            // First attempt: a bogus name so attach() fails.
            co_return segment_spec{
                page, posix_mmap_spec{"/partake-nonexistent-shmem-xyz", true}};
        }
        co_return spec_for(seg);
    });

    std::error_code first_ec;
    std::shared_ptr<mapping> retried;
    std::exception_ptr err;
    asio::co_spawn(
        ctx,
        [&]() -> asio::awaitable<void> {
            try {
                co_await cache.get(0);
            } catch (std::system_error const &e) {
                first_ec = e.code();
            }
            retried = co_await cache.get(0); // Entry was erased; re-fetches.
        },
        [&](std::exception_ptr e) { err = e; });
    ctx.run();

    REQUIRE_FALSE(err);
    CHECK(first_ec);           // First get threw.
    CHECK(fetch_count == 2);   // Retried after failure.
    CHECK(retried != nullptr); // Retry succeeded.
}

} // namespace partake::client::internal
