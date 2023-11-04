/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "daemon.hpp"

#include <tl/expected.hpp>

namespace partake::daemon {

// On error or help/version, prints message and returns exit code.
[[nodiscard]] auto parse_cli_args(int argc, char const *const *argv) noexcept
    -> tl::expected<daemon_config, int>;

} // namespace partake::daemon
