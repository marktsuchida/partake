/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "partake_protocol_generated.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace partake::client::internal {

class request_builder {
    using req_off = flatbuffers::Offset<protocol::Request>;

    flatbuffers::FlatBufferBuilder bldr;
    std::vector<req_off> req_offsets;
    std::size_t alloc_hint;

    // TODO Measure/Tune.
    static constexpr std::size_t approx_bytes_per_request = 64;

  public:
    explicit request_builder(std::size_t count_hint = 0)
        : bldr(approx_bytes_per_request * count_hint), alloc_hint(count_hint) {
    }

    [[nodiscard]] auto fbbuilder() noexcept
        -> flatbuffers::FlatBufferBuilder & {
        return bldr;
    }

    // request_offset must have been created using this.fbbuilder()
    template <typename R>
    void add_request(std::uint64_t seqno,
                     flatbuffers::Offset<R> request_offset) {
        auto req_enum = protocol::AnyRequestTraits<R>::enum_value;
        auto req = protocol::CreateRequest(bldr, seqno, req_enum,
                                           request_offset.Union());
        if (req_offsets.empty())
            req_offsets.reserve(alloc_hint);
        req_offsets.push_back(req);
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return req_offsets.empty();
    }

    // After call to this function, the instance may not be used.
    [[nodiscard]] auto release_buffer() -> flatbuffers::DetachedBuffer {
        auto req_vec = bldr.CreateVector(req_offsets);
        auto root = protocol::CreateRequestMessage(bldr, req_vec);
        bldr.FinishSizePrefixed(root);
        return bldr.Release();
    }
};

} // namespace partake::client::internal
