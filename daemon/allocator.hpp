/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "hive.hpp"

#include <boost/intrusive/list.hpp>
#include <doctest.h>
#include <gsl/span>

#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace partake::daemon {

namespace intru = boost::intrusive;

namespace internal {

template <typename T>
constexpr inline auto countl_zero_software_nonzero(T x) noexcept -> int {
    static_assert(std::is_unsigned_v<T>);
    int ret = 0;
    auto hi_bit = T(1) << (8 * sizeof(T) - 1);
    while ((x & hi_bit) == 0) {
        ++ret;
        hi_bit >>= 1;
    }
    return ret;
}

TEST_CASE("countl_zero software implementation") {
    CHECK(countl_zero_software_nonzero<std::uint16_t>(1u) == 15);
    CHECK(countl_zero_software_nonzero<std::uint16_t>(5u) == 13);
    CHECK(countl_zero_software_nonzero<std::uint16_t>(1u << 14) == 1);
    CHECK(countl_zero_software_nonzero<std::uint16_t>(1u << 15) == 0);
    CHECK(countl_zero_software_nonzero<std::uint32_t>(1u) == 31);
    CHECK(countl_zero_software_nonzero<std::uint32_t>(5u) == 29);
    CHECK(countl_zero_software_nonzero<std::uint32_t>(1u << 30) == 1);
    CHECK(countl_zero_software_nonzero<std::uint32_t>(1u << 31) == 0);
}

// C++20 has std::countl_zero() that can replace this. We implement for size_t
// only.
inline auto countl_zero(std::size_t x) noexcept -> int {
    if (x == 0)
        return 8 * sizeof(std::size_t);
#if defined(__GNUC__)
    static_assert(sizeof(std::size_t) == sizeof(unsigned long));
    return __builtin_clzl(x);
#elif defined(_MSC_VER)
    unsigned long trailing = 0;
    if constexpr (sizeof(std::size_t) == 8) {
        _BitScanReverse64(&trailing, x);
        return 63 - int(trailing);
    } else {
        _BitScanReverse(&trailing, static_cast<unsigned long>(x));
        return 31 - int(trailing);
    }
#else
    return countl_zero_software_nonzero(x);
#endif
}

TEST_CASE("countl_zero") {
    static constexpr auto size_bits = 8 * sizeof(std::size_t);
    CHECK(countl_zero(0) == size_bits);
    CHECK(countl_zero(1) == size_bits - 1);
    CHECK(countl_zero(5) == size_bits - 3);
    CHECK(countl_zero(std::size_t(1) << (size_bits - 2)) == 1);
    CHECK(countl_zero(std::size_t(1) << (size_bits - 1)) == 0);
}

inline auto free_list_index_for_size(std::size_t size) noexcept
    -> std::size_t {
    assert(size > 0);
    // At least for now, we use a separate free list for each size range whose
    // maximum is a power of 2: 1, 2, 4, ..., N, where N is the first power of
    // 2 that is greater than or equal to size.
    return 8 * sizeof(size) - static_cast<std::size_t>(countl_zero(size - 1));
}

TEST_CASE("free_list_index_for_size") {
    CHECK(free_list_index_for_size(1) == 0);
    CHECK(free_list_index_for_size(2) == 1);
    CHECK(free_list_index_for_size(3) == 2);
    CHECK(free_list_index_for_size(4) == 2);
    CHECK(free_list_index_for_size(5) == 3);
    CHECK(free_list_index_for_size(255) == 8);
    CHECK(free_list_index_for_size(256) == 8);
    CHECK(free_list_index_for_size(257) == 9);
}

// The arena performs allocation of chunks of some contiguous resource (e.g.,
// shared memory, of course). All bookkeeping happens externally (in regular
// memory owned by the arena), so that the shared memory is not actually
// touched (the arena has no interaction with the shared memory or other
// resource it manages). This has a number of advantages, including safety from
// client-side buffer overruns and reduced internal fragmentation when
// allocating chunks with large alignment.
//
// The arena operates on abstract blocks. Each allocated chunk consists of an
// integer number of contiguous blocks. Mapping the block size to some concrete
// (usually power-of-2) number of bytes must be done by client code.
//
// For now we have a single strategy using power-of-2 free lists and next-fit
// allocation, with eager coalescense of deallocated chunks.
//
// Because allocations track not just the start offset (as with the malloc()
// API) but also chunk size, we are able to make deallocation efficient (O(1))
// without storing metadata adjacent to (or inside) the chunk.
//
// In the event that optimization becomes necessary (perhaps due to excessive
// external fragmentation), it will probably make sense to use a layered
// allocator with pluggable strategies (analogous to std::pmr), which might
// include buddy and slab allocators. (Strategies based on multiple arenas (on
// multiple shared memory segments) should also be considered.)
class arena {
    struct adjacency_tag;
    using adjacency_base_hook =
        intru::list_base_hook<intru::tag<adjacency_tag>>;

    struct free_list_tag;
    using free_list_base_hook =
        intru::list_base_hook<intru::tag<free_list_tag>>;

    struct chunk : adjacency_base_hook, free_list_base_hook {
        std::size_t strt;
        std::size_t cnt;
        bool in_use;

        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        explicit chunk(std::size_t start, std::size_t count,
                       bool is_in_use) noexcept
            : strt(start), cnt(count), in_use(is_in_use) {}

        // No move or copy (used with intrusive data structures).
        auto operator=(chunk &&) = delete;
    };

    std::size_t siz; // In units of blocks.

    // Chunks are owned by 'chunk_storage' but participate in 'chunks', a
    // doubly-linked list of all chunks in monotonically increasing order of
    // start offset.
    // Chunks in use are also referenced by an allocation, which automatically
    // returns the chunk to the arena upon destruction.
    // Free chunks also participate in one of the free lists, which is a
    // doubly-linked list of free chunks of a particular size class. Currently,
    // the free lists are binned by power-of-2 thresholds: free_lists[N] holds
    // free chunks whose block count is at least 2^N and no more than 2^(N+1).

    hive<chunk> chunk_storage;

    intru::list<chunk, intru::base_hook<adjacency_base_hook>> chunks;

    using free_list =
        intru::list<chunk, intru::base_hook<free_list_base_hook>>;
    std::vector<free_list> free_lists;

  public:
    explicit arena(std::size_t size) noexcept : siz(size) {
        // Sentinels simplify coalescence of deallocated chunks. They are the
        // only 'chunk' instances with count == 0 and they never appear in free
        // lists.
        auto left_sentinel = chunk_storage.emplace(0, 0, true);
        auto right_sentinel = chunk_storage.emplace(size, 0, true);

        chunks.push_back(*left_sentinel);

        if (size > 0) {
            // Set up an initial free chunk occupying the whole size
            auto free_chunk = chunk_storage.emplace(0, size, false);
            chunks.push_back(*free_chunk);

            auto n_free_lists = internal::free_list_index_for_size(size) + 1;
            free_lists.resize(n_free_lists);
            free_lists.back().push_back(*free_chunk);
        }

        chunks.push_back(*right_sentinel);
    }

    // No move or copy (address taken by allocation instances)
    auto operator=(arena &&) = delete;

    [[nodiscard]] auto size() const noexcept -> std::size_t { return siz; }

    // RAII class for chunk allocation
    class allocation {
        // Both arn and chk are nullptr if default-initialized or allocation
        // failed. Otherwise they are both valid.
        arena *arn = nullptr;
        chunk *chk = nullptr;

        friend class arena;

        explicit allocation(arena *arena, chunk *chunk) noexcept
            : arn(arena), chk(chunk) {
            assert(not arn == not chk); // Both valid or both nullptr
        }

      public:
        allocation() noexcept = default;

        ~allocation() {
            if (arn != nullptr)
                arn->deallocate(chk);
        }

        // Noncopyable

        allocation(allocation &&other) noexcept
            : arn(std::exchange(other.arn, nullptr)),
              chk(std::exchange(other.chk, nullptr)) {}

        auto operator=(allocation &&rhs) noexcept -> allocation & {
            if (arn != nullptr)
                arn->deallocate(chk);
            arn = std::exchange(rhs.arn, nullptr);
            chk = std::exchange(rhs.chk, nullptr);
            return *this;
        }

        // True if allocation succeeded (even if count is zero).
        operator bool() const noexcept { return chk != nullptr; }

        [[nodiscard]] auto start() const noexcept -> std::size_t {
            return chk != nullptr ? chk->strt : 0;
        }

        [[nodiscard]] auto count() const noexcept -> std::size_t {
            return chk != nullptr ? chk->cnt : 0;
        }
    };

    [[nodiscard]] auto allocate(std::size_t count) noexcept -> allocation {
        // For the sake of regularity of behavior, we allow allocation of
        // zero-block chunks, but treat them as count 1 so that they have
        // distinct start offsets.
        if (count == 0)
            count = 1;

        for (auto &flist : free_lists_for_count(count)) {
            for (auto &chk : flist) {
                if (chk.cnt < count)
                    continue;

                // We will use chk. But before we go on, move the scanned
                // chunks to the end of the list so that they will not be
                // scanned first again next time. (If we don't do this, we
                // will likely accumulate smaller chunks at the beginning
                // of each free list, making allocations slower.)
                // (Sometimes known as the "next-fit" algorithm.)
                free_list wk;
                wk.splice(wk.end(), flist, flist.begin(),
                          flist.iterator_to(chk));
                flist.splice(flist.end(), wk);

                flist.erase(flist.iterator_to(chk));

                if (chk.cnt > count) { // Split off excess capacity
                    auto excess = chunk_storage.emplace(
                        chk.strt + count, chk.cnt - count, false);
                    chk.cnt = count;
                    free_list_for_count(excess->cnt).push_front(*excess);
                    chunks.insert(std::next(chunks.iterator_to(chk)), *excess);
                }

                chk.in_use = true;
                return allocation(this, &chk);
            }
        }

        return {}; // No large enough free chunk
    }

  private:
    [[nodiscard]] auto free_list_for_count(std::size_t count) noexcept
        -> free_list & {
        assert(count > 0);
        assert(count <= siz);
        return free_lists[internal::free_list_index_for_size(count)];
    }

    [[nodiscard]] auto free_lists_for_count(std::size_t count) noexcept
        -> gsl::span<free_list> {
        assert(count > 0);
        auto index = internal::free_list_index_for_size(count);
        index = std::min(index, free_lists.size());
        return gsl::span<free_list>(free_lists).subspan(index);
    }

    void deallocate(chunk *chk) noexcept {
        if (chk == nullptr)
            return;
        assert(chk->in_use);
        assert(chk->cnt > 0);

        chk->in_use = false;

        auto chkit = chunks.iterator_to(*chk);

        // Because of the left and right sentinel chunks, we do not need to
        // check for the case where we are deallocating the leftmost or
        // rightmost chunk of the arena.

        // Coalesce left
        auto prev = chkit;
        --prev;
        if (not prev->in_use) {
            auto &flist = free_list_for_count(prev->cnt);
            flist.erase(flist.iterator_to(*prev));
            chk->strt = prev->strt;
            chk->cnt += prev->cnt;
            chunks.erase(prev);
            chunk_storage.erase(chunk_storage.get_iterator(&*prev));
        }

        // Coalesce right
        auto next = chkit;
        ++next;
        if (not next->in_use) {
            auto &flist = free_list_for_count(next->cnt);
            flist.erase(flist.iterator_to(*next));
            chk->cnt += next->cnt;
            chunks.erase(next);
            chunk_storage.erase(chunk_storage.get_iterator(&*next));
        }

        free_list_for_count(chk->cnt).push_front(*chk);
    }
};

} // namespace internal

// Wrap arena to present an interface in terms of bytes instead of block
// counts.
template <typename Arena> class basic_allocator {
    Arena arn;
    std::size_t shift; // Block size == 2^shift

  public:
    explicit basic_allocator(std::size_t size,
                             std::size_t log2_block_size) noexcept
        : arn(size >> log2_block_size), shift(log2_block_size) {
        assert(log2_block_size < 8 * sizeof(std::size_t));
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return arn.size() << shift;
    }

    [[nodiscard]] auto log2_granularity() const noexcept -> std::size_t {
        return shift;
    }

    class allocation {
        typename Arena::allocation alloc;
        std::size_t shft;

        friend class basic_allocator;

        explicit allocation(typename Arena::allocation &&arena_allocation,
                            std::size_t shift) noexcept
            : alloc(std::move(arena_allocation)), shft(shift) {}

      public:
        allocation() noexcept : shft(0) {}

        operator bool() const noexcept { return bool(alloc); }

        [[nodiscard]] auto segment_id() const noexcept -> std::uint32_t {
            return 0;
        }

        [[nodiscard]] auto offset() const noexcept -> std::size_t {
            return alloc.start() << shft;
        }

        [[nodiscard]] auto size() const noexcept -> std::size_t {
            return alloc.count() << shft;
        }
    };

    [[nodiscard]] auto allocate(std::size_t size) noexcept -> allocation {
        auto count = size == 0 ? 0 : ((size - 1) >> shift) + 1;
        return allocation(arn.allocate(count), shift);
    }

    [[nodiscard]] auto arena() noexcept -> Arena & { return arn; }
};

using arena_allocator = basic_allocator<internal::arena>;

} // namespace partake::daemon
