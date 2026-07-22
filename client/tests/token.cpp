/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/token.hpp"

#include "proquint.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace partake::client {

TEST_CASE("token: default-constructed is invalid") {
    token const t;
    CHECK(not t.is_valid());
    CHECK(t.as_u64() == 0);
}

TEST_CASE("token: value roundtrip and validity") {
    token const t(0x1234'5678'9abc'def0uLL);
    CHECK(t.is_valid());
    CHECK(t.as_u64() == 0x1234'5678'9abc'def0uLL);
}

TEST_CASE("token: equality") {
    token const a(42);
    token const b(42);
    token const c(43);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(token() == token());
    CHECK(a != token());
}

TEST_CASE("token: usable in unordered containers") {
    std::unordered_set<token> s;
    s.insert(token(1));
    s.insert(token(2));
    s.insert(token(1));
    CHECK(s.size() == 2);
    CHECK(s.count(token(1)) == 1);
    CHECK(s.count(token(3)) == 0);
}

TEST_CASE("token: proquint roundtrip") {
    std::vector<std::uint64_t> const values = {
        1,
        42,
        0x1234'5678'9abc'def0uLL,
        0xffff'ffff'ffff'ffffuLL,
    };
    for (auto const v : values) {
        CAPTURE(v);
        auto const pq = token(v).to_proquint();
        auto const back = token::from_proquint(pq);
        CHECK(back.is_valid());
        CHECK(back == token(v));
    }
}

TEST_CASE("token: proquint format") {
    auto const pq = token(0x1234'5678'9abc'def0uLL).to_proquint();
    CHECK(pq.size() == 23);
    CHECK(pq[5] == '-');
    CHECK(pq[11] == '-');
    CHECK(pq[17] == '-');
    CHECK(pq == std::string(common::proquint64(0x1234'5678'9abc'def0uLL)));
}

TEST_CASE("token: from_proquint rejects malformed input") {
    CHECK(not token::from_proquint("").is_valid());
    CHECK(not token::from_proquint("lusab-babad").is_valid());
    auto const good = token(42).to_proquint();
    CHECK(token::from_proquint(good).is_valid());
    auto corrupt = good;
    corrupt[0] = 'q';
    CHECK(not token::from_proquint(corrupt).is_valid());
    auto truncated = good.substr(0, good.size() - 1);
    CHECK(not token::from_proquint(truncated).is_valid());
}

} // namespace partake::client
