/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cli.hpp"

#include "sizes.hpp"

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace partake::daemon {

namespace {

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
    double voucher_ttl = 10.0;
};

constexpr auto partake_version =
#ifdef PARTAKE_VERSION
#define PARTAKE_STRINGIFY_INTERNAL(s) #s                   // NOLINT
#define PARTAKE_STRINGIFY(s) PARTAKE_STRINGIFY_INTERNAL(s) // NOLINT
    PARTAKE_STRINGIFY(PARTAKE_VERSION);
#else
    "development build";
#endif

constexpr auto extra_help =
    R"(Memory size:
  A shared memory size that is a multiple of the platform page size
  must be given via --memory.

Client connection:
  You must pass --socket with a path name to use for the Unix domain
  socket (AF_UNIX socket) used for client connection. An absolute
  path is recommeded because the same path must also be given to
  clients.

Unix shared memory:
  [--posix] [--name=/myshmem]: Create with shm_open(2) and map with
      mmap(2). If name is given it should start with a slash and
      contain no more slashes.
  --systemv [--name=key]: Create with shmget(2) and map with shmat(2).
      If name is given it must be an integer key.
  --file=myfile: Create with open(2) and map with mmap(2). The --name
      option is ignored.
  Not all of the above may be available on a given Unix-like system.
  On Linux, huge pages can be allocated either by using --file with a
  location in a mounted hugetlbfs or by giving --huge-pages with
  --systemv. In both cases, --memory must be a multiple of the huge
  page size.

Windows shared memory:
  [--windows] [--name=Local\myshmem]: A named file mapping backed by
      the system paging file is created. If name is given it should
      start with "Local\" and contain no further backslashes.
  --file=myfile [--name=Local\myshmem]: A named file mapping backed
      by the given file is created. Usage of --name is the same as
      with --windows.
  On Windows, --large-pages can be specified with --windows (but not
  --file). This requires the user to have SeLockMemoryPrivilege. In
  this case, --memory must be a multiple of the large page size.

In all cases, partaked will exit with an error if the filename given
by --file or the name given by --name already exists, unless --force
is also given.)";

auto parse_nonempty(std::string const &s) noexcept -> std::string {
    if (s.empty())
        return "Argument must not be empty";
    return {};
}

auto parse_size_suffix(std::string const &s) noexcept(false) -> std::string {
    if (s.empty())
        throw CLI::ValidationError("Size must not be empty");

    std::size_t pos{};
    long long n{};
    try {
        n = std::stoll(s, &pos);
    } catch (std::invalid_argument const &) {
        throw CLI::ValidationError("Invalid size: " + s);
    } catch (std::out_of_range const &) {
        throw CLI::ValidationError("Size too large: " + s);
    }

    if (n < 0)
        throw CLI::ValidationError("Negative size not allowed: " + s);
    auto const nu = static_cast<unsigned long long>(n);

    if (pos == 0)
        throw CLI::ValidationError("Invalid size argument: " + s);
    auto const suffix = s.substr(pos);

    unsigned long long multiplier = 1;
    if (suffix.empty() || suffix == "B" || suffix == "b")
        ;
    else if (suffix == "K" || suffix == "k")
        multiplier = 1 << 10;
    else if (suffix == "M" || suffix == "m")
        multiplier = 1 << 20;
    else if (suffix == "G" || suffix == "g")
        multiplier = 1 << 30;
    else
        throw CLI::ValidationError("Invalid size suffix: " + suffix);

    auto const value = nu * multiplier;
    bool const overflowed = value / multiplier != nu;
    bool const too_large = sizeof(value) > sizeof(std::size_t) &&
                           value > std::numeric_limits<std::size_t>::max();
    if (overflowed || too_large)
        throw CLI::ValidationError("Size too large: " + s);

    return std::to_string(value);
}

TEST_CASE("parse_size_suffix") {
    CHECK(parse_size_suffix("0") == "0");
    CHECK(parse_size_suffix("1") == "1");
    CHECK(parse_size_suffix("12345") == "12345");

    CHECK(parse_size_suffix("0B") == "0");
    CHECK(parse_size_suffix("1B") == "1");
    CHECK(parse_size_suffix("12345B") == "12345");

    CHECK(parse_size_suffix("0k") == "0");
    CHECK(parse_size_suffix("1k") == "1024");
    CHECK(parse_size_suffix("12345k") == "12641280");

    CHECK(parse_size_suffix("0M") == "0");
    CHECK(parse_size_suffix("1M") == "1048576");

    CHECK(parse_size_suffix("0G") == "0");
    CHECK(parse_size_suffix("1G") == "1073741824");

    static constexpr auto max = [] {
        if constexpr (sizeof(std::size_t) == 4)
            return "2147483647"; // 2^31 - 1
        else {
            static_assert(sizeof(std::size_t) == 8);
            return "9223372036854775807"; // 2^63 - 1
        }
    }();
    CHECK(parse_size_suffix(max) == max);

    CHECK_THROWS_AS(parse_size_suffix(""), CLI::ValidationError const &);
    CHECK_THROWS_AS(parse_size_suffix("b"), CLI::ValidationError const &);
    CHECK_THROWS_AS(parse_size_suffix("-1"), CLI::ValidationError const &);
    CHECK_THROWS_AS(parse_size_suffix("1n"), CLI::ValidationError const &);
    CHECK_THROWS_AS(parse_size_suffix("1 B"), CLI::ValidationError const &);

    static constexpr auto max_plus_one = [] {
        if constexpr (sizeof(std::size_t) == 4)
            return "2147483648"; // 2^31
        else {
            static_assert(sizeof(std::size_t) == 8);
            return "9223372036854775808"; // 2^63
        }
    }();
    CHECK_THROWS_AS(parse_size_suffix(max_plus_one),
                    CLI::ValidationError const &);
}

auto parse_cli_args_unvalidated(int argc, char const *const *argv) noexcept
    -> tl::expected<cli_args, int> {
    using namespace std::string_literals;

    cli_args ret;

    CLI::App app;
    app.option_defaults()->disable_flag_override();
    app.description("The partake daemon.\n");
    app.footer(extra_help);

    // Make the help text fit in 79 columns. May require adjustment if option
    // description strings change.
    app.get_formatter()->column_width(26);

    app.add_option("-m,--memory", ret.memory,
                   "Size of shared memory (suffixes K/M/G allowed)")
        ->type_name("BYTES")
        ->check(parse_nonempty)
        ->transform(parse_size_suffix);

    app.add_option("-s,--socket", ret.socket,
                   "Filename of socket for client connection")
        ->type_name("NAME")
        ->check(parse_nonempty);

    app.add_option("-n,--name", ret.name,
                   "Name of shared memory (integer if --systemv)")
        ->type_name("NAME");
    // Allowed to be empty (autogenerate)

    app.add_option("-F,--file", ret.filename,
                   "Use shared memory backed by the given file")
        ->type_name("FILENAME")
        ->check(parse_nonempty);

    app.add_flag("-P,--posix", ret.posix,
                 "Use POSIX shm_open(2) shared memory (default)");

    app.add_flag("-S,--systemv", ret.systemv,
                 "Use System V shmget(2) shared memory");

    app.add_flag("-W,--windows", ret.windows,
                 "Use Win32 named shared memory (default on Windows)");

    app.add_option("-g,--granularity", ret.granularity,
                   "Allocation granularity (suffixes K/M/G allowed)")
        ->type_name("BYTES")
        ->transform(parse_size_suffix);

    app.add_flag("-H,--huge-pages", ret.huge_pages,
                 "Use Linux huge pages with --systemv");

    app.add_option("--huge-page-size", ret.huge_page_size,
                   "Select Linux huge page size (implies --huge-pages)")
        ->type_name("BYTES")
        ->transform(parse_size_suffix);

    app.add_flag("-L,--large-pages", ret.large_pages,
                 "Use Windows large pages");

    app.add_option("--voucher-ttl", ret.voucher_ttl,
                   fmt::format("Set voucher time-to-live (default: {} s)",
                               ret.voucher_ttl))
        ->type_name("SECONDS");

    app.add_flag("-f,--force", ret.force,
                 "Overwrite existing shared memory and/or file");

    app.set_help_flag("-h,--help", "Display this help and exit"s);
    app.set_version_flag("-V,--version", "partaked "s + partake_version);

    try {
        app.parse(argc, argv);
        return ret;
    } catch (CLI::ParseError const &err) { // Includes --help, --version
        return tl::unexpected(app.exit(err));
    }
}

enum class shmem_type { posix, system_v, win32, posix_file, win32_file };

template <bool IsWindows =
#ifdef _WIN32
              true
#else
              false
#endif
          >
auto validate_segment_type(cli_args const &args) noexcept
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

TEST_CASE("validate_segment_type") {
    cli_args args;
    CHECK(validate_segment_type<false>(args).value() == shmem_type::posix);
    CHECK(validate_segment_type<true>(args).value() == shmem_type::win32);

    args = cli_args();
    args.filename = "myfile";
    CHECK(validate_segment_type<false>(args).value() ==
          shmem_type::posix_file);
    CHECK(validate_segment_type<true>(args).value() == shmem_type::win32_file);

    args = cli_args();
    args.posix = true;
    CHECK(validate_segment_type(args).value() == shmem_type::posix);
    args = cli_args();
    args.systemv = true;
    CHECK(validate_segment_type(args).value() == shmem_type::system_v);
    args = cli_args();
    args.windows = true;
    CHECK(validate_segment_type(args).value() == shmem_type::win32);

    args = cli_args();
    args.posix = true;
    args.systemv = true;
    CHECK_FALSE(validate_segment_type(args).has_value());
    args = cli_args();
    args.posix = true;
    args.windows = true;
    CHECK_FALSE(validate_segment_type(args).has_value());
    args = cli_args();
    args.posix = true;
    args.filename = "x";
    CHECK_FALSE(validate_segment_type(args).has_value());
    args = cli_args();
    args.windows = true;
    args.filename = "x";
    CHECK_FALSE(validate_segment_type(args).has_value());
}

auto validate_posix_shmem_name(std::string const &name) noexcept
    -> tl::expected<std::string, std::string> {
    using namespace std::string_literals;
    if (name.empty())
        return name;
    if (name.front() != '/')
        return tl::unexpected(
            "POSIX shared memory name must start with a slash"s);
    if (name.size() == 1)
        return tl::unexpected(
            "POSIX shared memory name must contain characters after the initial slash"s);
    if (name.find_first_of('/', 1) != std::string::npos)
        return tl::unexpected(
            "POSIX shared memory name must not contain slashes after the initial slash"s);
    return name;
}

TEST_CASE("validate_posix_shmem_name") {
    CHECK(validate_posix_shmem_name("").value().empty());
    CHECK_FALSE(validate_posix_shmem_name("/").has_value());
    CHECK(validate_posix_shmem_name("/a").value() == "/a");
    CHECK_FALSE(validate_posix_shmem_name("/a/").has_value());
}

auto validate_sysv_shmem_name(std::string const &name) noexcept
    -> tl::expected<int, std::string> {
    using namespace std::string_literals;
    if (name.empty())
        return 0;
    try {
        return std::stoi(name);
    } catch (std::invalid_argument const &) {
        return tl::unexpected(
            "System V shared memory name (key) must be an integer"s);
    } catch (std::out_of_range const &) {
        return tl::unexpected(
            "System V shared memory name (key) must be an integer in the 32-bit range"s);
    }
}

TEST_CASE("validate_sysv_shmem_name") {
    CHECK(validate_sysv_shmem_name("").value() == 0);
    CHECK(validate_sysv_shmem_name("0").value() == 0);
    CHECK(validate_sysv_shmem_name("1").value() == 1);
    CHECK(validate_sysv_shmem_name("-1").value() == -1);
    CHECK_FALSE(validate_sysv_shmem_name("2147483648").has_value());
    CHECK_FALSE(validate_sysv_shmem_name("abc").has_value());
}

auto validate_win32_shmem_name(std::string const &name) noexcept
    -> tl::expected<std::string, std::string> {
    using namespace std::string_literals;
    static auto const required_prefix = R"(Local\)"s;
    if (name.empty())
        return name;
    if (name.substr(0, required_prefix.size()) != required_prefix)
        return tl::unexpected(
            R"(Windows shared memory name must have the prefix "Local\")"s);
    if (name.size() == required_prefix.size())
        return tl::unexpected(
            R"(Windows shared memory name must contain characters after the prefix "Local\")"s);
    if (name.find_first_of('\\', required_prefix.size()) != std::string::npos)
        return tl::unexpected(
            R"(Windows shared memory name must not contain backslashes after the prefix "Local\")"s);
    return name;
}

TEST_CASE("validate_win32_shmem_name") {
    CHECK(validate_win32_shmem_name("").value().empty());
    CHECK_FALSE(validate_win32_shmem_name("x").has_value());
    CHECK_FALSE(validate_win32_shmem_name(R"(Local\)").has_value());
    CHECK(validate_win32_shmem_name(R"(Local\x)").value() == R"(Local\x)");
    CHECK_FALSE(validate_win32_shmem_name(R"(Local\x\)").has_value());
}

auto validate_segment_config(cli_args const &args) noexcept
    -> tl::expected<segment_config, std::string> {
    using namespace std::string_literals;
    auto const type_or_error = validate_segment_type(args);
    if (not type_or_error.has_value())
        return tl::unexpected(type_or_error.error());
    auto const type = *type_or_error;

    bool const use_huge_pages = args.huge_pages || args.huge_page_size > 0;
    if (use_huge_pages && type != shmem_type::system_v)
        return tl::unexpected("--huge-pages requires System V shared memory"s);
    if (args.large_pages && type != shmem_type::win32)
        return tl::unexpected(
            "--large-pages requires Windows (non-file-backed) shared memory"s);

    switch (type) {
    case shmem_type::posix: {
        auto const maybe_name = validate_posix_shmem_name(args.name);
        if (not maybe_name.has_value())
            return tl::unexpected(maybe_name.error());
        return segment_config{
            posix_mmap_segment_config{*maybe_name, args.force}, args.memory};
    }
    case shmem_type::system_v: {
        auto const maybe_key = validate_sysv_shmem_name(args.name);
        if (not maybe_key.has_value())
            return tl::unexpected(maybe_key.error());
        return segment_config{sysv_segment_config{*maybe_key, args.force,
                                                  use_huge_pages,
                                                  args.huge_page_size},
                              args.memory};
    }
    case shmem_type::win32: {
        auto const maybe_name = validate_win32_shmem_name(args.name);
        if (not maybe_name.has_value())
            return tl::unexpected(maybe_name.error());
        return segment_config{
            win32_segment_config{
                {}, *maybe_name, args.force, args.large_pages},
            args.memory};
    }
    case shmem_type::posix_file:
        return segment_config{
            file_mmap_segment_config{args.filename, args.force}, args.memory};
    case shmem_type::win32_file:
        return segment_config{
            win32_segment_config{args.filename, args.name, args.force, false},
            args.memory};
    default:
        assert(false);
        std::terminate();
    }
}

auto validate_cli_args(cli_args const &args) noexcept
    -> tl::expected<daemon_config, std::string> {
    using namespace std::string_literals;
    daemon_config ret;

    if (args.memory == 0)
        return tl::unexpected(
            "--memory is required and its argument must be positive"s);

    // Unix domain socket path names have a low length limit. Linux and Windows
    // limits are 107, (some?) BSDs have 103, and apparently some Unices have
    // limits as low as 91. However, we defer the length check to Asio.
    if (args.socket.empty())
        return tl::unexpected("--socket is required"s);
    ret.endpoint = args.socket;

    if (args.granularity > 0) {
        if (not is_size_power_of_2(args.granularity))
            return tl::unexpected(
                "Allocation granularity must be a power of 2"s);
        static constexpr std::size_t min_gran = 512;
        if (args.granularity < min_gran) {
            return tl::unexpected(fmt::format(
                "Allocation granularity must not be less than {}", min_gran));
        }
        ret.log2_granularity = log2_size(args.granularity);
    }

    if (args.voucher_ttl <= 0.0)
        return tl::unexpected("Voucher time-to-live must be positive"s);
    auto const fp_seconds = std::chrono::duration<double>(args.voucher_ttl);
    ret.voucher_ttl =
        std::chrono::duration_cast<std::chrono::milliseconds>(fp_seconds);

    return validate_segment_config(args).map(
        [&ret](segment_config const &seg_cfg) {
            ret.seg_config = seg_cfg;
            return ret;
        });
}

} // namespace

auto parse_cli_args(int argc, char const *const *argv) noexcept
    -> tl::expected<daemon_config, int> {
    return parse_cli_args_unvalidated(argc, argv)
        .and_then([](cli_args const &args) {
            return validate_cli_args(args).map_error(
                [](std::string const &msg) {
                    std::cerr << msg << '\n';
                    std::cerr << "Run with --help for more information.\n";
                    return 1;
                });
        });
}

} // namespace partake::daemon
