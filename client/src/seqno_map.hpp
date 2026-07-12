/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace partake::client::internal {

// Data structure mapping unique and consecutive seqnos to T, with performance
// properties that make sense for sequential numbers that will be added in
// order and typically will be removed _roughly_ in the same order. May not be
// very efficient if a large number of sparse entries live for unusually long
// durations; if this becomes a problem, we may need to compress long-lasting
// entries into a separate data structure.
template <typename T> class seqno_map {
  public:
    using seqno_type = std::uint64_t;

  private:
    // Chunk size should be power of 2 to eliminate division.
    static constexpr std::size_t chunk_size = 128;

    // The next unused seqno.
    seqno_type next_no;

    // Seqno of first entry in first chunk (or the chunk to be created next if
    // chunks is empty). Always a multiple of chunk_size.
    seqno_type oldest_chunk_start;

    struct chunk {
        std::bitset<chunk_size> in_use;
        std::array<T, chunk_size> entries{};
    };
    std::vector<std::unique_ptr<chunk>> chunks;

    auto indices(seqno_type no) noexcept
        -> std::pair<std::size_t, std::size_t> {
        auto const offset = no - oldest_chunk_start;
        auto const chunk_index = offset / chunk_size;
        auto const index_within_chunk = offset % chunk_size;
        return {chunk_index, index_within_chunk};
    }

  public:
    explicit seqno_map(seqno_type first_seqno = 0)
        : next_no(first_seqno),
          oldest_chunk_start(first_seqno / chunk_size * chunk_size) {}

    [[nodiscard]] auto next_seqno() const noexcept -> seqno_type {
        return next_no;
    }

    // Mark the next seqno as in use, and return a reference to the entry.
    // The returned entry is in default-constructed state.
    auto push() -> T & {
        auto const [chunk_index, index_within_chunk] = indices(next_no);
        chunks.resize(std::max(chunks.size(), chunk_index + 1));
        auto &chk = chunks[chunk_index];
        if (not chk)
            chk = std::make_unique<chunk>();
        // No throwing operations from here on.
        chk->in_use[index_within_chunk] = true;
        ++next_no;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return chk->entries[index_within_chunk];
    }

    [[nodiscard]] auto peek(seqno_type seqno) -> T & {
        auto const [chunk_index, index_within_chunk] = indices(seqno);
        if (chunk_index >= chunks.size())
            throw std::runtime_error("Unknown seqno");
        auto &chk = chunks[chunk_index];
        if (not chk || not chk->in_use[index_within_chunk])
            throw std::runtime_error("Unknown seqno");
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return chk->entries[index_within_chunk];
    }

    // Behavior undefined unless seqno present (peek first).
    void pop(seqno_type seqno) {
        auto const [chunk_index, index_within_chunk] = indices(seqno);
        assert(chunk_index < chunks.size());
        auto &chk = chunks[chunk_index];
        assert(chk && chk->in_use[index_within_chunk]);
        chk->in_use[index_within_chunk] = false;

        // Purge finished chunks if this chunk transitioned to empty _and_ will
        // not be re-populated with the next seqno.
        if (not chk->in_use.any()) {
            auto const next_chunk_index = indices(next_no).first;
            if (next_chunk_index != chunk_index) {
                chk.reset();
                // beg..ed = chunks before the one containing next_no
                auto beg = chunks.begin();
                auto ed = next_chunk_index >= chunks.size()
                              ? chunks.end()
                              : std::next(chunks.begin(),
                                          std::ptrdiff_t(next_chunk_index));
                // beg..keep = contiguous finished chunks at front of vector
                auto keep = std::find_if(beg, ed, [](auto const &chk) {
                    return chk && chk->in_use.any();
                });
                auto const n_purged = std::size_t(std::distance(beg, keep));
                chunks.erase(beg, keep);
                oldest_chunk_start += n_purged * chunk_size;
            }
        }
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return std::all_of(chunks.begin(), chunks.end(), [](auto const &chk) {
            return not chk || not chk->in_use.any();
        });
    }

    // func: void(seqno_type, T &)
    template <typename F> void for_each(F func) {
        auto chunk_start = oldest_chunk_start;
        for (auto &chk : chunks) {
            if (chk) {
                for (seqno_type i = 0; i < chunk_size; ++i) {
                    if (chk->in_use[i]) {
                        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                        func(chunk_start + i, chk->entries[i]);
                    }
                }
            }
            chunk_start += chunk_size;
        }
    }
};

} // namespace partake::client::internal
