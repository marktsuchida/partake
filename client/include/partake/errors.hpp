/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <system_error>
#include <type_traits>

namespace partake::client {

// Daemon-reported statuses (mirrors protocol::Status for values 1-6;
// category "partake protocol"). Note: unknown_protocol_error is 7 here,
// whereas common::protocol_errc uses -1 for it; the implementation maps
// between the two when producing messages.
enum class protocol_errc {
    invalid_request = 1,
    out_of_shmem,
    no_such_segment,
    no_such_object,
    object_busy,
    object_reserved,
    unknown_protocol_error,
};

// Client-side conditions (category "partake client").
enum class client_errc {
    canceled = 1,   // Op detached via cancel().
    disconnected,   // Connection closed/failed before completion.
    connect_failed, // Could not establish connection.
    unsupported_protocol_version, // Server rejected our protocol version.
    protocol_violation,           // Malformed or unexpected daemon response.
    object_closed,                // Op submitted on a closed objview.
};

[[nodiscard]] auto protocol_category() noexcept -> std::error_category const &;

[[nodiscard]] auto client_category() noexcept -> std::error_category const &;

inline auto make_error_code(protocol_errc c) noexcept -> std::error_code {
    return {static_cast<int>(c), protocol_category()};
}

inline auto make_error_code(client_errc c) noexcept -> std::error_code {
    return {static_cast<int>(c), client_category()};
}

} // namespace partake::client

namespace std {

template <>
struct is_error_code_enum<partake::client::protocol_errc> : true_type {};

template <>
struct is_error_code_enum<partake::client::client_errc> : true_type {};

} // namespace std
