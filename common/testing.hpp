/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "posix.hpp"
#include "random.hpp"
#include "win32.hpp"

#include <doctest.h>
#include <fmt/core.h>
#include <gsl/span>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace partake::testing {

// A temporary directory created for the scope of a test. It must be empty when
// leaving the scope, or else we fail.
class tempdir {
    std::filesystem::path p;

    static auto tdp() -> std::filesystem::path {
#ifdef __APPLE__
        // Avoid the long path returned by temp_directory_path(), because it is
        // too long to use as a Unix domain socket name.
        return "/tmp";
#else
        return std::filesystem::temp_directory_path();
#endif
    }

    static constexpr auto rand_suffix_len = 6;

  public:
    tempdir()
        : p(tdp() /
            ("partake-test-" + common::random_string(rand_suffix_len))) {
        REQUIRE_FALSE(std::filesystem::exists(p));
        std::filesystem::create_directories(p);
    }

    ~tempdir() {
        std::error_code ec;
        std::filesystem::remove(p, ec);
        if (ec) {
            spdlog::error("Failed to remove test temporary directory {}: {}",
                          p.string(), ec.message());
            std::terminate();
        }
    }

    tempdir(tempdir const &) = delete;
    auto operator=(tempdir const &) = delete;
    tempdir(tempdir &&) = delete;
    auto operator=(tempdir &&) = delete;

    [[nodiscard]] auto path() const -> std::filesystem::path { return p; }
};

inline auto make_test_filename(std::string_view sourcefile, unsigned lineno)
    -> std::string {
    auto p = std::filesystem::path(sourcefile).filename();
    return fmt::format("test.{}.L{}", p.string(), lineno);
}

inline auto unique_path(std::filesystem::path const &parent,
                        std::string const &hint) -> std::filesystem::path {
    static constexpr auto rand_suffix_len = 20;
    auto fname = hint + "-" + common::random_string(rand_suffix_len);
    auto p = parent / fname;
    REQUIRE_FALSE(std::filesystem::exists(p));
    return p;
}

class auto_delete_file {
    std::filesystem::path p;

  public:
    explicit auto_delete_file(std::filesystem::path path)
        : p(std::move(path)) {}

    ~auto_delete_file() {
        if (p.empty())
            return;
        std::error_code ec;
        std::filesystem::remove(p, ec);
        if (ec) {
            spdlog::error("Failed to remove test file {}: {}", p.string(),
                          ec.message());
            std::terminate();
        }
    }

    auto_delete_file(auto_delete_file const &) = delete;
    auto operator=(auto_delete_file const &) = delete;
    auto_delete_file(auto_delete_file &&) = delete;
    auto operator=(auto_delete_file &&) = delete;
};

class unique_file_with_data {
    std::filesystem::path p;
    auto_delete_file adf;

  public:
    explicit unique_file_with_data(std::filesystem::path const &parent,
                                   std::string const &hint,
                                   gsl::span<std::uint8_t const> data)
        : p(unique_path(parent, hint)), adf(p) {
        std::ofstream s(p, std::ios::binary);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        s.write(reinterpret_cast<char const *>(data.data()),
                static_cast<std::streamsize>(data.size()));
    }

    [[nodiscard]] auto path() const -> std::filesystem::path { return p; }
};

} // namespace partake::testing
