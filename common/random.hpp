/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace partake::common {

auto random_string(std::size_t len) noexcept -> std::string;

} // namespace partake::common
