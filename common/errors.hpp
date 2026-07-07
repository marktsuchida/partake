/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <system_error>
#include <type_traits>

namespace partake::common {

// Hint: See https://akrzemi1.wordpress.com/2017/07/12/your-own-error-code/
// for an introduction to how std::error_code works.

enum class errc {
    message_too_long = 1,
    invalid_message,
    misaligned_message,
    eof_in_message,
    invalid_request_type,
};

// Mirror protocol::Status so that it can be handled as a std::error_code
// together with other types of errors on the client side.
enum class protocol_errc {
    unknown_protocol_error = -1,
    invalid_request = 1,
    out_of_shmem,
    no_such_segment,
    no_such_object,
    object_busy,
    object_reserved,
};

struct partake_error_category : std::error_category {
    [[nodiscard]] auto name() const noexcept -> char const * override {
        return "partake";
    }

    [[nodiscard]] auto message(int c) const -> std::string override {
        switch (static_cast<errc>(c)) {
        case errc::message_too_long:
            return "Protocol message exceeds allowed size";
        case errc::invalid_message:
            return "Malformed or incompatible protocol message";
        case errc::misaligned_message:
            return "Protocol message frame length is not a multiple of 8";
        case errc::eof_in_message:
            return "End-of-file encountered before end of message";
        case errc::invalid_request_type:
            return "Invalid or incompatible request type";
        }
        if (c == 0)
            return "Success";
        return "Unknown error (partake)";
    }
};

struct protocol_error_category : std::error_category {
    [[nodiscard]] auto name() const noexcept -> char const * override {
        return "partake_protocol";
    }

    [[nodiscard]] auto message(int c) const -> std::string override {
        switch (static_cast<protocol_errc>(c)) {
        case protocol_errc::unknown_protocol_error:
            break;
        case protocol_errc::invalid_request:
            return "Invalid request";
        case protocol_errc::out_of_shmem:
            return "Out of shared memory";
        case protocol_errc::no_such_segment:
            return "No such segment";
        case protocol_errc::no_such_object:
            return "No object or voucher with given key and expected type";
        case protocol_errc::object_busy:
            return "Object is not ready for this operation";
        case protocol_errc::object_reserved:
            return "Object already has pending unshare request";
        }
        if (c == 0)
            return "Success";
        return "Unknown error (partake protocol)";
    }
};

inline partake_error_category const the_partake_error_category;
inline protocol_error_category const the_protocol_error_category;

inline auto make_error_code(errc c) noexcept -> std::error_code {
    return {static_cast<int>(c), the_partake_error_category};
}

inline auto make_error_code(protocol_errc c) noexcept -> std::error_code {
    return {static_cast<int>(c), the_protocol_error_category};
}

} // namespace partake::common

namespace std {

template <> struct is_error_code_enum<partake::common::errc> : true_type {};

template <>
struct is_error_code_enum<partake::common::protocol_errc> : true_type {};

} // namespace std
