/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace partake::client {

// Identifies a submitted async operation; unique per client instance. The
// value 0 is never issued and means "no operation".
using op_id = std::uint64_t;

#ifdef _WIN32
// A SOCKET (AF_UNIX socket pair; future stage -- not yet implemented).
using wakeup_handle = std::uintptr_t;
#else
// A file descriptor: the read end of a pipe.
using wakeup_handle = int;
#endif

// Mirrors protocol::Policy; the enumerator values must stay compatible.
enum class policy : std::uint8_t {
    default_,
    primitive,
};

enum class op_type : std::int32_t {
    none,
    connect,
    ping,
    alloc,
    open,
    close,
    share,
    unshare,
    create_voucher,
    discard_voucher,
    shutdown,
};

} // namespace partake::client
