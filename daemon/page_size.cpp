/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "page_size.hpp"

#include <doctest.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#ifdef _WIN32
// clang-format off
#include <Windows.h>
#include <Memoryapi.h>
// clang-format on
#else
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/vfs.h>
#endif

namespace partake::daemon {

auto page_size() noexcept -> std::size_t {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    ::GetSystemInfo(&sysinfo);
    return sysinfo.dwPageSize;
#else
    return static_cast<std::size_t>(::getpagesize());
#endif
}

TEST_CASE("page_size") {
    auto const p = page_size();
    CHECK(p > 0);
    bool const is_power_of_2 = (p & (p - 1)) == 0;
    CHECK(is_power_of_2);
}

#ifdef _WIN32

auto system_allocation_granularity() noexcept -> std::size_t {
    SYSTEM_INFO sysinfo;
    ::GetSystemInfo(&sysinfo);
    return sysinfo.dwAllocationGranularity;
}

TEST_CASE("system_allocation_granularity") {
    auto const g = system_allocation_granularity();
    CHECK(g >= page_size());
    CHECK(g % page_size() == 0);
    bool const is_power_of_2 = (g & (g - 1)) == 0;
    CHECK(is_power_of_2);
}

auto large_page_minimum() noexcept -> std::size_t {
    return ::GetLargePageMinimum();
}

#endif

namespace {

auto read_default_huge_page_size(std::istream &meminfo) noexcept
    -> std::size_t {
    if (!meminfo)
        return 0;
    std::string line;
    while (std::getline(meminfo, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key != "Hugepagesize:")
            continue;
        std::string siz;
        std::string unit;
        ls >> siz >> unit;
        if (unit != "kB")
            return 0;
        std::string word;
        ls >> word;
        if (not word.empty())
            return 0;
        std::size_t len = 0;
        std::size_t s = 0;
        try {
            s = std::stoul(siz, &len);
        } catch (std::exception const &) {
            return 0;
        }
        if (len < siz.size())
            return 0;
        return s * 1024;
    }
    return 0;
}

TEST_CASE("read_default_huge_page_size") {
    std::istringstream strm;

    SUBCASE("empty file") { CHECK(read_default_huge_page_size(strm) == 0); }

    SUBCASE("missing key") {
        strm.str("Aaa: bbb");
        CHECK(read_default_huge_page_size(strm) == 0);
    }

    SUBCASE("typical") {
        strm.str("Aaa:  bbb\nHugepagesize:  1024 kB\nCcc:  ddd");
        CHECK(read_default_huge_page_size(strm) == 1048576);
    }

    SUBCASE("missing value") {
        strm.str("Hugepagesize:");
        CHECK(read_default_huge_page_size(strm) == 0);
    }

    SUBCASE("missing unit") {
        strm.str("Hugepagesize: 1024");
        CHECK(read_default_huge_page_size(strm) == 0);
    }

    SUBCASE("wrong unit") {
        strm.str("Hugepagesize: 1024 MB");
        CHECK(read_default_huge_page_size(strm) == 0);
    }

    SUBCASE("extra token") {
        strm.str("Hugepagesize: 1024 kB  blah");
        CHECK(read_default_huge_page_size(strm) == 0);
    }
}

[[maybe_unused]] auto get_default_huge_page_size() noexcept -> std::size_t {
    std::ifstream meminfo("/proc/meminfo");
    // Factor out parsing for testability.
    return read_default_huge_page_size(meminfo);
}

// "hugepages-2048kB" -> 2097152
auto parse_huge_page_filename(std::string const &name) noexcept
    -> std::size_t {
    static std::string const prefix("hugepages-");
    if (name.rfind(prefix, 0) == std::string::npos)
        return 0;
    std::string const size_kb = name.substr(prefix.size());
    std::size_t len = 0;
    std::size_t s = 0;
    try {
        s = std::stoul(size_kb, &len);
    } catch (std::exception const &) {
        return 0;
    }
    if (size_kb.substr(len) != "kB")
        return 0;
    return s * 1024;
}

TEST_CASE("parse_huge_page_filename") {
    CHECK(parse_huge_page_filename("") == 0);
    CHECK(parse_huge_page_filename("hugepages-xxx") == 0);
    CHECK(parse_huge_page_filename("hugepages-1024") == 0);
    CHECK(parse_huge_page_filename("hugepages-kB") == 0);
    CHECK(parse_huge_page_filename("hugepages-1024kB") == 1048576);
    CHECK(parse_huge_page_filename("hugepages-1024MB") == 0);
    CHECK(parse_huge_page_filename("hugepage-1024kB") == 0);
}

[[maybe_unused]] auto get_huge_page_sizes() noexcept
    -> std::vector<std::size_t> {
    std::vector<std::size_t> ret;
    auto const dflt = get_default_huge_page_size();
    if (dflt > 0)
        ret.push_back(dflt);
    std::error_code ec;
    std::filesystem::directory_iterator const dirit("/sys/kernel/mm/hugepages",
                                                    ec);
    if (not ec) {
        for (auto const &dirent : dirit) {
            auto const nm = dirent.path().filename().string();
            std::size_t const siz = parse_huge_page_filename(nm);
            if (siz > 0 && siz != dflt)
                ret.push_back(siz);
        }
    }
    std::sort(ret.begin(), ret.end());
    return ret;
}

} // namespace

#ifdef __linux__

auto default_huge_page_size() noexcept -> std::size_t {
    static std::size_t const ret = get_default_huge_page_size();
    return ret;
}

TEST_CASE("default_huge_page_size") {
    std::size_t const result = default_huge_page_size();
    if (result > 0) {
        bool const is_power_of_2 = (result & (result - 1)) == 0;
        CHECK(is_power_of_2);
    }
}

auto huge_page_sizes() noexcept -> std::vector<std::size_t> {
    static std::vector<std::size_t> const ret = get_huge_page_sizes();
    return ret;
}

TEST_CASE("huge_page_sizes") {
    auto const result = huge_page_sizes();
    std::size_t prev = 0;
    for (auto s : result) {
        CHECK(s > prev); // Unique, sorted, and > 0.
        prev = s;
        bool const is_power_of_2 = (s & (s - 1)) == 0;
        CHECK(is_power_of_2);
    }
}

auto file_page_size(int fd) noexcept -> std::size_t {
    struct statfs st {};
    if (::fstatfs(fd, &st))
        return 0;
    static constexpr decltype(st.f_type) hugetlbfs_magic = 0x958458f6;
    if (st.f_type != hugetlbfs_magic)
        return page_size();
    return static_cast<std::size_t>(st.f_bsize);
}

#endif

auto round_up_or_check_size(std::size_t &size,
                            std::size_t granularity) noexcept -> bool {
    // Do not automatically round up to granularity >= 1 MiB, so as not to
    // accidentally allocate unexpectedly large blocks.
    static constexpr std::size_t threshold = 1u << 20;
    if (granularity == 0) {
        spdlog::error("Could not determine correct allocation granularity");
        return false;
    }
    auto div = size / granularity;
    auto mod = size % granularity;
    if (mod == 0)
        return true;
    if (granularity < threshold) {
        auto const rounded_up = (div + 1) * granularity;
        spdlog::warn("Requested size ({}) rounded up to {}",
                     human_readable_size(size),
                     human_readable_size(rounded_up));
        size = rounded_up;
        return true;
    }
    spdlog::error(
        "Requested size ({}) is not a multiple of the required granularity ({})",
        human_readable_size(size), human_readable_size(granularity));
    spdlog::info(
        "Automatic round up is disabled when the required granularity is >= {}",
        human_readable_size(threshold));
    return false;
}

TEST_CASE("round_up_or_check_size") {
    std::size_t size = 0;
    CHECK(round_up_or_check_size(size, 4096));
    CHECK(size == 0);
    size = 1;
    CHECK(round_up_or_check_size(size, 4096));
    CHECK(size == 4096);
    CHECK(round_up_or_check_size(size, 4096));
    CHECK(size == 4096);

    size = 0;
    CHECK(round_up_or_check_size(size, 1048576));
    CHECK(size == 0);
    size = 1;
    CHECK_FALSE(round_up_or_check_size(size, 1048576));
    CHECK(size == 1);
    size = 1048576;
    CHECK(round_up_or_check_size(size, 1048576));
    CHECK(size == 1048576);
}

auto human_readable_size(std::size_t size) noexcept -> std::string {
    // Do not round numbers; only summarize when exact.
    static constexpr std::size_t sub_kilo_mask = (1u << 10) - 1;
    if (size == 0)
        return "0 bytes";
    if (size == 1)
        return "1 byte";
    if (size & sub_kilo_mask)
        return fmt::format("{} bytes", size);
    size >>= 10;
    if (size & sub_kilo_mask)
        return fmt::format("{} KiB", size);
    size >>= 10;
    if (size & sub_kilo_mask)
        return fmt::format("{} MiB", size);
    size >>= 10;
    if (size & sub_kilo_mask)
        return fmt::format("{} GiB", size);
    size >>= 10;
    if (size & sub_kilo_mask)
        return fmt::format("{} TiB", size);
    size >>= 10;
    if (size & sub_kilo_mask)
        return fmt::format("{} PiB", size);
    size >>= 10;
    return fmt::format("{} EiB", size);
}

TEST_CASE("human_readable_size") {
    CHECK(human_readable_size(0) == "0 bytes");
    CHECK(human_readable_size(1) == "1 byte");
    CHECK(human_readable_size(2) == "2 bytes");
    CHECK(human_readable_size(1023) == "1023 bytes");
    CHECK(human_readable_size(1024) == "1 KiB");
    CHECK(human_readable_size(1025) == "1025 bytes");
    CHECK(human_readable_size(1u << 20) == "1 MiB");
    CHECK(human_readable_size(1u << 30) == "1 GiB");
}

} // namespace partake::daemon
