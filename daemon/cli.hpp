/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "config.hpp"
#include "daemon.hpp"

#include <tl/expected.hpp>

#include <cstddef>
#include <string>

namespace partake::daemon {

namespace internal {

struct cli_args {
    std::size_t memory = 0;
    std::string socket;
    std::string name;
    std::string filename;
    bool posix = false;
    bool systemv = false;
    bool windows = false;
    std::size_t granularity = 0;
    bool huge_pages = false;
    std::size_t huge_page_size = 0;
    bool large_pages = false;
    bool force = false;
    double voucher_ttl = default_voucher_ttl_seconds;
};

// Throws CLI::ValidationError on invalid input.
auto parse_size_suffix(std::string const &s) -> std::string;

enum class shmem_type { posix, system_v, win32, posix_file, win32_file };

template <bool IsWindows =
#ifdef _WIN32
              true
#else
              false
#endif
          >
auto validate_segment_type(cli_args const &args)
    -> tl::expected<shmem_type, std::string> {
    using namespace std::string_literals;
    int const shmem_type_count = int(args.posix) + int(args.systemv) +
                                 int(args.windows) +
                                 int(not args.filename.empty());
    if (shmem_type_count > 1)
        return tl::unexpected(
            "Only one of --posix, --systemv, --windows, --file may be given"s);
    if (args.posix)
        return shmem_type::posix;
    if (args.systemv)
        return shmem_type::system_v;
    if (args.windows)
        return shmem_type::win32;
    if (not args.filename.empty()) {
        if constexpr (IsWindows)
            return shmem_type::win32_file;
        else
            return shmem_type::posix_file;
    }
    if constexpr (IsWindows)
        return shmem_type::win32;
    else
        return shmem_type::posix;
}

auto validate_posix_shmem_name(std::string const &name)
    -> tl::expected<std::string, std::string>;

auto validate_sysv_shmem_name(std::string const &name)
    -> tl::expected<int, std::string>;

auto validate_win32_shmem_name(std::string const &name)
    -> tl::expected<std::string, std::string>;

} // namespace internal

// On error or help/version, prints message and returns exit code.
[[nodiscard]] auto parse_cli_args(int argc, char const *const *argv)
    -> tl::expected<daemon_config, int>;

} // namespace partake::daemon
