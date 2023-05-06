/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <chrono>

namespace partake::daemon {

using clock = std::chrono::steady_clock;
using time_point = clock::time_point;

} // namespace partake::daemon
