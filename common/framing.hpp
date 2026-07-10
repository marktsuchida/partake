/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>

namespace partake::common {

// Framing convention: each wire message is a standard size-prefixed
// FlatBuffer whose total length (4-byte prefix + payload) is a multiple of 8.
// Senders append zero padding and round the size prefix up to cover it; the
// padding is unreachable via FlatBuffers offsets, so accessors and the
// verifier never touch it. This keeps successive frames 8-byte aligned in the
// receive buffer for in-place parsing.
constexpr std::size_t message_frame_alignment = 8;
constexpr std::size_t max_message_frame_len = 32768;

constexpr auto round_size_up_to_message_frame_alignment(std::size_t s) noexcept
    -> std::size_t {
    return (s + message_frame_alignment - 1) & ~(message_frame_alignment - 1);
}

} // namespace partake::common
