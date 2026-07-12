/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/token.hpp"

#include "proquint.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace partake::client {

auto token::to_proquint() const -> std::string {
    return std::string(common::proquint64(t));
}

auto token::from_proquint(std::string_view s) -> token {
    auto const pq = common::proquint64::validate(s);
    return pq ? token(std::uint64_t(*pq)) : token{};
}

} // namespace partake::client
