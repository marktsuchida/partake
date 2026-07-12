/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/errors.hpp"

// client/src has no errors.hpp, so this resolves to common's.
#include "errors.hpp"

#include <string>
#include <system_error>

namespace partake::client {

namespace {

struct protocol_category_t final : std::error_category {
    [[nodiscard]] auto name() const noexcept -> char const * override {
        return "partake protocol";
    }

    [[nodiscard]] auto message(int c) const -> std::string override {
        // Our unknown_protocol_error is 7; common's is -1.
        auto const public_unknown =
            static_cast<int>(protocol_errc::unknown_protocol_error);
        auto const common_unknown =
            static_cast<int>(common::protocol_errc::unknown_protocol_error);
        auto const mapped = c == public_unknown ? common_unknown : c;
        return common::the_protocol_error_category.message(mapped);
    }
};

struct client_category_t final : std::error_category {
    [[nodiscard]] auto name() const noexcept -> char const * override {
        return "partake client";
    }

    [[nodiscard]] auto message(int c) const -> std::string override {
        switch (static_cast<client_errc>(c)) {
        case client_errc::canceled:
            return "Operation canceled by caller";
        case client_errc::disconnected:
            return "Connection closed before operation completed";
        case client_errc::connect_failed:
            return "Could not establish connection";
        case client_errc::unsupported_protocol_version:
            return "Server rejected protocol version";
        case client_errc::protocol_violation:
            return "Malformed or unexpected daemon response";
        case client_errc::object_closed:
            return "Operation submitted on closed object";
        }
        if (c == 0)
            return "Success";
        return "Unknown error (partake client)";
    }
};

protocol_category_t const the_protocol_category;
client_category_t const the_client_category;

} // namespace

auto protocol_category() noexcept -> std::error_category const & {
    return the_protocol_category;
}

auto client_category() noexcept -> std::error_category const & {
    return the_client_category;
}

} // namespace partake::client
