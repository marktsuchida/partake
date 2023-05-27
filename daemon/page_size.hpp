/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace partake::daemon {

// Return the system's regular page size. On Windows, do not confuse with
// system_allocation_granularity().
auto page_size() noexcept -> std::size_t;

#ifdef _WIN32

// Return the system's regular allocation granularity (a multiple of page size
// on Windows). This value, rather than page_size(), should be used for offsets
// and sizes of mappings when using regular page size.
auto system_allocation_granularity() noexcept -> std::size_t;

// Return the minimum large page size. Return 0 if large pages not supported.
auto large_page_minimum() noexcept -> std::size_t;

#endif

#ifdef __linux__

// Return the default huge page size. Return 0 if huge pages not supported.
auto default_huge_page_size() noexcept -> std::size_t;

// Return the available huge page sizes in increasing order. Empty if huge
// pages not supported.
auto huge_page_sizes() noexcept -> std::vector<std::size_t>;

// Return the page size for the given fd, taking hugetlbfs into account.
auto file_page_size(int fd) noexcept -> std::size_t;

#endif

// Round 'size' up to a multiple of page_size and return true if granularity is
// small. If granularity is large, return true if size is already a multiple of
// granularity; otherwise return false. Return false if granularity is zero.
auto round_up_or_check_size(std::size_t &size,
                            std::size_t granularity) noexcept -> bool;

auto human_readable_size(std::size_t size) noexcept -> std::string;

} // namespace partake::daemon
