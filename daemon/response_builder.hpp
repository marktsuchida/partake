/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "framing.hpp"
#include "partake_protocol_generated.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace partake::daemon {

// Builds ResponseMessage frames, emitting them via the given sink. To keep
// every frame within max_message_frame_len, the in-progress message is
// emitted and a fresh one started whenever its projected size exceeds a soft
// threshold. The rotation check runs after each response is added (a response
// is serialized into the current builder before we see its size, and
// FlatBuffers offsets cannot be moved to another builder), so the hard bound
// on a frame is soft_max_frame_size plus one whole response. The headroom
// between the soft threshold and max_message_frame_len must therefore cover
// the largest possible single response: GetSegmentResponse carrying a file
// path (bounded by PATH_MAX, ~4 KiB) plus table overhead; all other responses
// are tens of bytes. 8 KiB is comfortably enough.
class response_builder {
    using resp_off = flatbuffers::Offset<protocol::Response>;

    std::function<void(flatbuffers::DetachedBuffer &&)> emit;
    flatbuffers::FlatBufferBuilder bldr;
    std::vector<resp_off> resp_offsets;
    std::size_t alloc_hint;

    static constexpr std::size_t approx_bytes_per_response = 64;
    static constexpr std::size_t max_single_response_size = 8192;
    static constexpr std::size_t soft_max_frame_size =
        common::max_message_frame_len - max_single_response_size;

  public:
    explicit response_builder(
        std::function<void(flatbuffers::DetachedBuffer &&)> emit_message,
        std::size_t count_hint = 0)
        : emit(std::move(emit_message)),
          bldr(approx_bytes_per_response * count_hint),
          alloc_hint(count_hint) {}

    [[nodiscard]] auto fbbuilder() noexcept
        -> flatbuffers::FlatBufferBuilder & {
        return bldr;
    }

    // response_offset must have been created using this.fbbuilder()
    template <typename R>
    void add_successful_response(std::uint64_t seqno,
                                 flatbuffers::Offset<R> response_offset) {
        auto resp_enum = protocol::AnyResponseTraits<R>::enum_value;
        auto resp =
            protocol::CreateResponse(bldr, seqno, protocol::Status::OK,
                                     resp_enum, response_offset.Union());
        add_response(resp);
    }

    void add_error_response(std::uint64_t seqno, protocol::Status status) {
        assert(status != protocol::Status::OK);
        auto resp = protocol::CreateResponse(bldr, seqno, status);
        add_response(resp);
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return resp_offsets.empty();
    }

    // Emit the in-progress message, if any. The builder remains usable.
    void flush() {
        if (not resp_offsets.empty())
            emit_current();
    }

  private:
    void add_response(resp_off resp) {
        // Minimize allocations by deferring until first response added.
        if (resp_offsets.empty())
            resp_offsets.reserve(alloc_hint);

        resp_offsets.push_back(resp);

        if (projected_frame_size() > soft_max_frame_size) {
            // Only violated by a single response larger than the headroom.
            assert(projected_frame_size() <= common::max_message_frame_len);
            emit_current();
        }
    }

    [[nodiscard]] auto projected_frame_size() const noexcept -> std::size_t {
        // Finished frame = current content + responses vector (4-byte length
        // + 4 bytes per offset) + ResponseMessage table + root offset +
        // 4-byte size prefix, padded to 8. finish_overhead conservatively
        // covers everything but the per-offset cost.
        static constexpr std::size_t finish_overhead = 64;
        return common::round_size_up_to_message_frame_alignment(
            bldr.GetSize() + (4 * resp_offsets.size()) + finish_overhead);
    }

    void emit_current() {
        auto resp_vec = bldr.CreateVector(resp_offsets);
        auto root = protocol::CreateResponseMessage(bldr, resp_vec);
        bldr.FinishSizePrefixed(root);
        emit(bldr.Release()); // Release() clears the builder for reuse.
        resp_offsets.clear();
    }
};

} // namespace partake::daemon
