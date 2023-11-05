/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "proquint.hpp"

#include <doctest.h>

#include <gsl/span>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace partake::common::internal {

namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// NOLINTBEGIN(readability-magic-numbers)

#ifndef DOCTEST_CONFIG_DISABLE

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

#endif // DOCTEST_CONFIG_DISABLE

// Shifts *pi left after consuming the high 4 bytes
auto hi4bits_to_consonant(u16 &pi) noexcept -> char {
    using namespace std::string_view_literals;
    static constexpr auto consonants = "bdfghjklmnprstvz"sv;
    u16 j = pi & 0b1111'0000'0000'0000;
    pi = static_cast<u16>(pi << 4);
    j = static_cast<u16>(j >> 12);
    return consonants[j];
}

// Shifts *pi left after consuming the high 2 bytes
auto hi2bits_to_vowel(u16 &pi) noexcept -> char {
    using namespace std::string_view_literals;
    static constexpr auto vowels = "aiou"sv;
    u16 j = pi & 0b1100'0000'0000'0000;
    pi = static_cast<u16>(pi << 2);
    j = static_cast<u16>(j >> 14);
    return vowels[j];
}

void uint16_to_proquint(u16 i, gsl::span<char, 5> dest) noexcept {
    dest[0] = hi4bits_to_consonant(i);
    dest[1] = hi2bits_to_vowel(i);
    dest[2] = hi4bits_to_consonant(i);
    dest[3] = hi2bits_to_vowel(i);
    dest[4] = hi4bits_to_consonant(i);
}

} // namespace

void proquint_from_u64(gsl::span<char, 23> dest, std::uint64_t i) noexcept {
    uint16_to_proquint(u16(i >> 48), dest.subspan<0, 5>());
    dest[5] = '-';
    uint16_to_proquint(u16(i >> 32), dest.subspan<6, 5>());
    dest[11] = '-';
    uint16_to_proquint(u16(i >> 16), dest.subspan<12, 5>());
    dest[17] = '-';
    uint16_to_proquint(u16(i >> 0), dest.subspan<18, 5>());
}

#ifndef DOCTEST_CONFIG_DISABLE

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

#endif // DOCTEST_CONFIG_DISABLE

namespace {

// dest is shifted left before appending 4 bits
auto consonant_to_4bits(char ch, u16 &dest) noexcept -> bool {
    using namespace std::string_view_literals;
    static constexpr auto table = "\xff" // a
                                  "\x00" // b
                                  "\xff" // c
                                  "\x01" // d
                                  "\xff" // e
                                  "\x02" // f
                                  "\x03" // g
                                  "\x04" // h
                                  "\xff" // i
                                  "\x05" // j
                                  "\x06" // k
                                  "\x07" // l
                                  "\x08" // m
                                  "\x09" // n
                                  "\xff" // o
                                  "\x0a" // p
                                  "\xff" // q
                                  "\x0b" // r
                                  "\x0c" // s
                                  "\x0d" // t
                                  "\xff" // u
                                  "\x0e" // v
                                  "\xff" // w
                                  "\xff" // x
                                  "\xff" // y
                                  "\x0f" // z
                                  ""sv;
    dest = static_cast<u16>(dest << 4);
    u8 const idx = u8(ch) - u8('a');
    if (idx >= table.size() || u8(table[idx]) == 0xffu)
        return false;
    dest |= u8(table[idx]);
    return true;
}

// dest is shifted left before appending 2 bits
auto vowel_to_2bits(char ch, u16 &dest) noexcept -> bool {
    using namespace std::string_view_literals;
    static constexpr auto table = "\x00" // a
                                  "\xff" // b
                                  "\xff" // c
                                  "\xff" // d
                                  "\xff" // e
                                  "\xff" // f
                                  "\xff" // g
                                  "\xff" // h
                                  "\x01" // i
                                  "\xff" // j
                                  "\xff" // k
                                  "\xff" // l
                                  "\xff" // m
                                  "\xff" // n
                                  "\x02" // o
                                  "\xff" // p
                                  "\xff" // q
                                  "\xff" // r
                                  "\xff" // s
                                  "\xff" // t
                                  "\x03" // u
                                  "\xff" // v
                                  "\xff" // w
                                  "\xff" // x
                                  "\xff" // y
                                  "\xff" // z
                                  ""sv;
    dest = static_cast<u16>(dest << 2);
    u8 const idx = u8(ch) - u8('a');
    if (idx >= table.size() || u8(table[idx]) == 0xffu)
        return false;
    dest |= u8(table[idx]);
    return true;
}

auto proquint_to_u16(gsl::span<char const, 5> pq) noexcept
    -> std::pair<u16, bool> {
    u16 result = 0;
    bool ok = true; // Minimize branches
    ok &= consonant_to_4bits(pq[0], result);
    ok &= vowel_to_2bits(pq[1], result);
    ok &= consonant_to_4bits(pq[2], result);
    ok &= vowel_to_2bits(pq[3], result);
    ok &= consonant_to_4bits(pq[4], result);
    return {result, ok};
}

} // namespace

auto proquint_to_u64(gsl::span<char const, 23> pq) noexcept
    -> std::pair<std::uint64_t, bool> {
    u64 result = 0;
    bool ok = true; // Minimize branches

    auto const [wk0, ok0] = proquint_to_u16(pq.subspan<0, 5>());
    result |= wk0;
    ok &= ok0;

    result <<= 16;
    ok &= pq[5] == '-';

    auto const [wk1, ok1] = proquint_to_u16(pq.subspan<6, 5>());
    result |= wk1;
    ok &= ok1;

    result <<= 16;
    ok &= pq[11] == '-';

    auto const [wk2, ok2] = proquint_to_u16(pq.subspan<12, 5>());
    result |= wk2;
    ok &= ok2;

    result <<= 16;
    ok &= pq[17] == '-';

    auto const [wk3, ok3] = proquint_to_u16(pq.subspan<18, 5>());
    result |= wk3;
    ok &= ok3;

    return {result, ok};
}

#ifndef DOCTEST_CONFIG_DISABLE

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

#endif // DOCTEST_CONFIG_DISABLE

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
