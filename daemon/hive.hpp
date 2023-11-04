/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <plf_colony.h>

namespace partake::daemon {

// Let's use the name 'hive' in our code. The plf::colony container is proposed
// for standardization (https://wg21.link/p0447) under the name std::hive.
// (There is also plf::hive, whose interface matches the proposal, but it
// requires C++20+.)
template <typename T> using hive = plf::colony<T>;

} // namespace partake::daemon
