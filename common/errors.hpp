/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <system_error>
#include <type_traits>

namespace partake::daemon {

// Hint: See https://akrzemi1.wordpress.com/2017/07/12/your-own-error-code/
// for an introduction to how std::error_code works.

enum class errc {
    message_too_long = 1,
    invalid_message,
    eof_in_message,
    invalid_request_type,
};

struct partake_error_category : std::error_category {
    [[nodiscard]] auto name() const noexcept -> char const * override {
        return "partake";
    }

    [[nodiscard]] auto message(int c) const noexcept -> std::string override {
        switch (static_cast<errc>(c)) {
        case errc::message_too_long:
            return "Protocol message exceeds allowed size";
        case errc::invalid_message:
            return "Malformed or incompatible protocol message";
        case errc::eof_in_message:
            return "End-of-file encountered before end of message";
        case errc::invalid_request_type:
            return "Invalid or incompatible request type";
        default:
            if (c == 0)
                return "Success";
            return "Unknown error";
        }
    }
};

inline partake_error_category const the_partake_error_category;

inline auto make_error_code(errc c) noexcept -> std::error_code {
    return {static_cast<int>(c), the_partake_error_category};
}

} // namespace partake::daemon

namespace std {

template <> struct is_error_code_enum<partake::daemon::errc> : true_type {};

} // namespace std
