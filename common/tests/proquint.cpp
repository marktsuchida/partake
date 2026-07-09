/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "proquint.hpp"

#include <catch2/catch_test_macros.hpp>

#include <gsl/span>

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace partake::common::internal {

namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

// NOLINTBEGIN(readability-magic-numbers)

std::vector<std::pair<u64, std::string_view>> const pq_test_data{
    {0uLL, "babab-babab-babab-babab"},
    {u64(1), "babab-babab-babab-babad"},
    {u64(2), "babab-babab-babab-babaf"},
    {u64(-1), "zuzuz-zuzuz-zuzuz-zuzuz"},
    {u64(-2), "zuzuz-zuzuz-zuzuz-zuzuv"},
    {u64(u32(-1)), "babab-babab-zuzuz-zuzuz"},

    // Sample data from the proquint spec (converting IPv4 to hex and grouping
    // to 64-bit), which happens to cover all vowels and all consonants.
    {0x7F00'0001'3F54'DCC1uLL, "lusab-babad-gutih-tugad"},
    {0x3F76'0723'8C62'C18DuLL, "gutuk-bisog-mudof-sakat"},
    {0x40FF'06C8'801E'342DuLL, "haguz-biram-mabiv-gibot"},
    {0x9343'7702'D43A'FD44uLL, "natag-lisaf-tibup-zujah"},
    {0xD823'44D7'D844'E815uLL, "tobog-higil-todah-vobij"},
    {0xC651'8188'0C6E'6ECCuLL, "sinid-makam-budov-kuras"},
};

} // namespace

TEST_CASE("u64 to proquint") {
    std::string dest;
    dest.resize(23);
    auto span = gsl::span<char, 23>(dest);
    for (auto [n, pq] : pq_test_data) {
        // Structured binding doesn't work with lambda capture of CHECK().
        auto const proq = pq;
        proquint_from_u64(span, n);
        CHECK(dest == std::string(proq));
    }
}

TEST_CASE("proquint to u64") {
    for (auto [n, pq] : pq_test_data) {
        // Structured binding doesn't work with lambda capture of CHECK().
        auto const number = n;
        auto const proq = pq;

        CHECK(proq.size() == 23);
        auto pqspan = gsl::span<char const, 23>(pq);
        std::uint64_t result{};
        bool ok{};
        std::tie(result, ok) = proquint_to_u64(pqspan);
        CHECK(ok);
        CHECK(result == number);
    }
}

TEST_CASE("invalid proquint64") {
    static std::vector<std::string> const bad_pq{
        "",
        "b",
        "cabab-babab-babab-babab",
        "abbab-babab-babab-babab",
        "babab-babab-babab-baba",
        "abab-babab-babab-babab",
        "babab-babab-babab-babab-",
        "babab-babab-babab-bababa",
        "Babab-babab-babab-babab",
        "babab-babab.babab-babab",
        "babab-baba-bbabab-babab",
    };

    for (auto const &pq : bad_pq) {
        auto r = proquint64::validate(pq);
        CHECK_FALSE(r.has_value());
    }
}

TEST_CASE("proquint64 equality") {
    CHECK(proquint64(123) == proquint64(123));
    CHECK(proquint64(123) != proquint64(124));
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::common::internal
