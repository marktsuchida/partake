/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "partake/errors.hpp"

#include "partake_protocol_generated.h"
#include "request_builder.hpp"

#include <gsl/span>

#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

// Wire-level helpers for the per-operation request/response codecs: frame
// verification, hello construction, and the encode (add_*_request) and
// decode (decode_*_response) function pairs consumed by connection_impl's
// request<>() primitive.

namespace partake::client::internal {

struct ping_result {};
struct quit_result {};

[[nodiscard]] auto get_process_id() -> std::uint32_t;

[[nodiscard]] auto
verify_server_hello_message(gsl::span<std::uint8_t const> bytes) -> bool;

[[nodiscard]] auto verify_response_message(gsl::span<std::uint8_t const> bytes)
    -> bool;

// Maps to the public partake::client::protocol_errc: OK -> success;
// unrecognized values -> unknown_protocol_error.
[[nodiscard]] auto error_code_for_status(protocol::Status status) noexcept
    -> std::error_code;

// Size-prefixed ClientHelloMessage{CURRENT, get_process_id(), name}.
[[nodiscard]] auto make_client_hello(std::string_view name)
    -> flatbuffers::DetachedBuffer;

void add_ping_request(request_builder &rb, std::uint64_t seqno);
void add_quit_request(request_builder &rb, std::uint64_t seqno);

// nullopt = wrong or missing union member (a protocol violation).
[[nodiscard]] auto decode_ping_response(protocol::Response const &resp)
    -> std::optional<ping_result>;
[[nodiscard]] auto decode_quit_response(protocol::Response const &resp)
    -> std::optional<quit_result>;

} // namespace partake::client::internal
