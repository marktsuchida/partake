/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "token_hash_table.hpp"

#include <doctest.h>

namespace partake::daemon {

namespace {

// Element must inherit hook and have key() method.
struct elem : token_hash_table<elem>::hook {
    common::token ky;
    elem(common::token key) : ky(key) {}
    [[nodiscard]] auto key() const -> common::token { return ky; }
};

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("token_hash_table") {
    using common::token;
    token_hash_table<elem> t;
    CHECK(t.empty());
    CHECK(t.find(token(42)) == t.end());
    elem e{token(42)};
    t.insert(e);
    CHECK(t.find(token(42))->ky.as_u64() == 42);
    CHECK(t.iterator_to(e) == t.find(token(42)));
    t.erase(t.iterator_to(e)); // Table must be empty before destruction.
}

TEST_CASE("token_hash_table: foreach") {
    using common::token;
    token_hash_table<elem> t;
    elem e{token(42)};
    elem f{token(43)};
    elem g{token(44)};
    t.insert(e);
    t.insert(f);
    t.insert(g);
    // We cannot erase elements within a range-for loop, because we can't get
    // the 'next' of an invalidated iterator. But we can do this:
    for (auto i = t.begin(), ed = t.end(); i != ed;) {
        auto next = std::next(i);
        t.erase(i);
        i = next;
    }
}

TEST_CASE("token_hash_table: rehash") {
    token_hash_table<elem> t;
    t.rehash_if_appropriate(true);

    std::vector<elem> v;
    v.reserve(1000);
    for (std::uint64_t i = 0; i < 1000; ++i)
        v.emplace_back(common::token(i));

    for (elem &e : v) {
        t.insert(e);
        t.rehash_if_appropriate();
    }

    while (not t.empty()) {
        t.erase(t.begin());
        t.rehash_if_appropriate(true);
    }
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::daemon
