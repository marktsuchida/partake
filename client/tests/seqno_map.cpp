/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "seqno_map.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

namespace partake::client::internal {

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("seqno_map") {
    struct entry {
        int v = 0;
    };

    // Test with first_seqno chosen to involve seqno rollover.
    std::uint64_t const first_begin =
        std::numeric_limits<std::uint64_t>::max() - 258;
    std::uint64_t const first_end = 258;

    SECTION("one by one") {
        for (std::uint64_t first = first_begin; first != first_end; ++first) {
            CAPTURE(first);
            seqno_map<entry> m(first);
            REQUIRE(m.empty());
            for (int v = 0; v < 130; ++v) {
                std::uint64_t no = first + std::uint64_t(v);
                CAPTURE(no);
                REQUIRE(m.next_seqno() == no);
                auto &ent = m.push();
                REQUIRE(m.next_seqno() == no + 1);
                REQUIRE(not m.empty());
                ent.v = v;
                REQUIRE(m.peek(no).v == v);
                REQUIRE(&m.peek(no) == &ent);
                m.pop(no);
                REQUIRE(m.empty());
            }
        }
    }

    SECTION("many at once, popped in order") {
        for (std::uint64_t first = first_begin; first != first_end; ++first) {
            CAPTURE(first);
            seqno_map<entry> m(first);
            for (int v = 0; v < 258; ++v) {
                auto &ent = m.push();
                ent.v = v;
            }
            REQUIRE(not m.empty());
            for (int v = 0; v < 258; ++v) {
                std::uint64_t const no = first + std::uint64_t(v);
                auto const &ent = m.peek(no);
                REQUIRE(ent.v == v);
                m.pop(no);
            }
            REQUIRE(m.empty());
        }
    }

    SECTION("many at once, popped in reverse order") {
        for (std::uint64_t first = first_begin; first != first_end; ++first) {
            CAPTURE(first);
            seqno_map<entry> m(first);
            for (int v = 0; v < 258; ++v) {
                auto &ent = m.push();
                ent.v = v;
            }
            REQUIRE(not m.empty());
            for (int v = 257; v >= 0; --v) {
                std::uint64_t const no = first + std::uint64_t(v);
                auto const &ent = m.peek(no);
                REQUIRE(ent.v == v);
                m.pop(no);
            }
            REQUIRE(m.empty());
        }
    }
}

TEST_CASE("seqno_map for_each") {
    seqno_map<int> m(0);
    m.for_each([](std::uint64_t, int &) { REQUIRE(false); });
    m.push() = 42;
    m.push() = 43;
    m.push() = 44;
    m.pop(1);
    CHECK(not m.empty());
    std::vector<std::uint64_t> seqnos;
    std::vector<int> entries;
    m.for_each([&](std::uint64_t seqno, int &entry) {
        seqnos.push_back(seqno);
        entries.push_back(entry);
    });
    CHECK(seqnos == std::vector<std::uint64_t>{0, 2});
    CHECK(entries == std::vector{42, 44});
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::client::internal
