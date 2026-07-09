/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "page_size.hpp"

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

#ifdef _WIN32

auto system_allocation_granularity() noexcept -> std::size_t {
    SYSTEM_INFO sysinfo;
    ::GetSystemInfo(&sysinfo);
    return sysinfo.dwAllocationGranularity;
}

auto large_page_minimum() noexcept -> std::size_t {
    return ::GetLargePageMinimum();
}

#endif

namespace internal {

auto read_default_huge_page_size(std::istream &meminfo) -> std::size_t {
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

auto parse_huge_page_filename(std::string const &name) -> std::size_t {
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

} // namespace internal

namespace {

[[maybe_unused]] auto get_default_huge_page_size() -> std::size_t {
    std::ifstream meminfo("/proc/meminfo");
    // Factor out parsing for testability.
    return internal::read_default_huge_page_size(meminfo);
}

[[maybe_unused]] auto get_huge_page_sizes() -> std::vector<std::size_t> {
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
            std::size_t const siz = internal::parse_huge_page_filename(nm);
            if (siz > 0 && siz != dflt)
                ret.push_back(siz);
        }
    }
    std::sort(ret.begin(), ret.end());
    return ret;
}

} // namespace

#ifdef __linux__

auto default_huge_page_size() -> std::size_t {
    static std::size_t const ret = get_default_huge_page_size();
    return ret;
}

auto huge_page_sizes() -> std::vector<std::size_t> {
    static std::vector<std::size_t> const ret = get_huge_page_sizes();
    return ret;
}

auto file_page_size(int fd) -> std::size_t {
    struct statfs st{};
    if (::fstatfs(fd, &st) != 0)
        return 0;
    static constexpr decltype(st.f_type) hugetlbfs_magic = 0x958458f6;
    if (st.f_type != hugetlbfs_magic)
        return page_size();
    return static_cast<std::size_t>(st.f_bsize);
}

#endif

} // namespace partake::daemon
