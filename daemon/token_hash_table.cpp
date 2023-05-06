/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "token_hash_table.hpp"

#include <doctest.h>

namespace partake::daemon {

namespace {

// Element must inherit hook and have token() method.
struct elem : token_hash_table<elem>::hook {
    btoken tok;
    elem(btoken token) : tok(token) {}
    [[nodiscard]] auto token() const -> btoken { return tok; }
};

} // namespace

TEST_CASE("token_hash_table") {
    token_hash_table<elem> t;
    CHECK(t.empty());
    CHECK(t.find(42) == t.end());
    elem e{42};
    t.insert(e);
    CHECK(t.find(42)->tok == 42);
    CHECK(t.iterator_to(e) == t.find(42));
    t.erase(t.iterator_to(e)); // Table must be empty before destruction.
}

TEST_CASE("token_hash_table: foreach") {
    token_hash_table<elem> t;
    elem e{42};
    elem f{43};
    elem g{44};
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
    for (int i = 0; i < 1000; ++i)
        v.emplace_back(i);

    for (elem &e : v) {
        t.insert(e);
        t.rehash_if_appropriate();
    }

    while (not t.empty()) {
        t.erase(t.begin());
        t.rehash_if_appropriate(true);
    }
}

} // namespace partake::daemon
