/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

// Compiled as C++17 with only the public include directory, proving that
// the public headers are complete, self-contained, and stdlib-only.

#include "partake/partake.hpp"

#ifdef _MSVC_LANG
static_assert(_MSVC_LANG == 201703L);
#else
static_assert(__cplusplus == 201703L);
#endif

// Anchor symbol so the archive is not empty (silences macOS ranlib).
auto partake_client_cxx17_header_check_anchor() -> int { return 0; }
