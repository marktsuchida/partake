/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partake_protocol_generated.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace partake::daemon {

class response_builder {
    using resp_off = flatbuffers::Offset<protocol::Response>;

    flatbuffers::FlatBufferBuilder bldr;
    std::vector<resp_off> resp_offsets;
    std::size_t alloc_hint;

    static constexpr std::size_t approx_bytes_per_response = 64;

  public:
    explicit response_builder(std::size_t count_hint = 0) noexcept
        : bldr(approx_bytes_per_response * count_hint),
          alloc_hint(count_hint) {}

    [[nodiscard]] auto fbbuilder() noexcept
        -> flatbuffers::FlatBufferBuilder & {
        return bldr;
    }

    // response_offset must have been created using this.fbbuilder()
    template <typename R>
    void
    add_successful_response(std::uint64_t seqno,
                            flatbuffers::Offset<R> response_offset) noexcept {
        auto resp_enum = protocol::AnyResponseTraits<R>::enum_value;
        auto resp =
            protocol::CreateResponse(bldr, seqno, protocol::Status::OK,
                                     resp_enum, response_offset.Union());
        add_response(resp);
    }

    void add_error_response(std::uint64_t seqno,
                            protocol::Status status) noexcept {
        assert(status != protocol::Status::OK);
        auto resp = protocol::CreateResponse(bldr, seqno, status);
        add_response(resp);
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return resp_offsets.empty();
    }

    // After call to this function, the instance may not be used.
    [[nodiscard]] auto release_buffer() noexcept
        -> flatbuffers::DetachedBuffer {
        auto resp_vec = bldr.CreateVector(resp_offsets);
        auto root = protocol::CreateResponseMessage(bldr, resp_vec);
        bldr.FinishSizePrefixed(root);
        return bldr.Release();
    }

  private:
    void add_response(resp_off resp) noexcept {
        // Minimize allocations by deferring until first response added.
        if (resp_offsets.empty())
            resp_offsets.reserve(alloc_hint);

        resp_offsets.push_back(resp);
    }
};

} // namespace partake::daemon
