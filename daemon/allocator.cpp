/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "allocator.hpp"

#include <doctest.h>
#include <trompeloeil.hpp>

namespace {

using namespace partake::daemon;

TEST_CASE("arena") {
    using internal::arena;
    CHECK(arena(0).size() == 0);
    CHECK(arena(1).size() == 1);
    CHECK(arena(10).size() == 10);

    CHECK_FALSE(arena(0).allocate(1));

    auto a = arena(8);
    auto a0 = a.allocate(1);
    REQUIRE(a0);
    REQUIRE(a0.count() == 1);
    auto a1 = a.allocate(2);
    REQUIRE(a1);
    REQUIRE(a1.count() == 2);
    auto a2 = a.allocate(4);
    REQUIRE(!a.allocate(2));
    auto a3 = a.allocate(0);
    REQUIRE(a3);
    REQUIRE(a3.count() == 1);
    REQUIRE(!a.allocate(1));
    { auto discard = std::move(a1); }
    auto a4 = a.allocate(1);
    REQUIRE(a4);
    REQUIRE(a4.count() == 1);
    auto a5 = a.allocate(1);
    REQUIRE(a5);
    REQUIRE(a5.count() == 1);
    { auto discard = std::move(a4); }
    { auto discard = std::move(a5); }
    // Check coalescence of neighboring a4 and a5 (former a1)
    auto a6 = a.allocate(2);
    REQUIRE(a6);
    REQUIRE(a6.count() == 2);
}

TEST_CASE("arena: large sizes") {
    using internal::arena;
    auto b = arena(std::size_t(-1));
    CHECK(b.allocate(std::size_t(-1)).count() == std::size_t(-1));
    auto c = arena(std::size_t(-2));
    CHECK_FALSE(c.allocate(std::size_t(-1)));
}

struct fake_arena_allocation {
    std::size_t s;
    std::size_t c;
    [[nodiscard]] auto start() const noexcept -> std::size_t { return s; }
    [[nodiscard]] auto count() const noexcept -> std::size_t { return c; }
    operator bool() const noexcept { return c > 0; }
};

class mock_arena {
    std::size_t cnt;

  public:
    using allocation = fake_arena_allocation;
    explicit mock_arena(std::size_t count) : cnt(count) {}
    MAKE_MOCK1(allocate, allocation(std::size_t)); // NOLINT
    auto size() const noexcept -> std::size_t { return cnt; }
};

TEST_CASE("allocator") {
    basic_allocator<mock_arena> a(9, 1);
    CHECK(a.arena().size() == 4);
    CHECK(a.size() == 8);

    SUBCASE("typical allocation") {
        REQUIRE_CALL(a.arena(), allocate(3))
            .RETURN(fake_arena_allocation{42, 3});
        auto alloc = a.allocate(5);
        CHECK(alloc.offset() == 84);
        CHECK(alloc.size() == 6);
    }

    SUBCASE("zero-byte allocation passes through") {
        REQUIRE_CALL(a.arena(), allocate(0))
            .RETURN(fake_arena_allocation{0, 1});
        auto alloc = a.allocate(0);
        CHECK(alloc.size() == 2);
    }

    SUBCASE("failed allocation") {
        REQUIRE_CALL(a.arena(), allocate(100))
            .RETURN(fake_arena_allocation{0, 0});
        auto alloc = a.allocate(200);
        CHECK_FALSE(alloc);
    }
}

} // namespace
