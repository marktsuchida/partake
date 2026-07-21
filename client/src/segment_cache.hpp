/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "mapping.hpp"
#include "requests.hpp" // segment_spec
#include "unique_handler.hpp"

#include "asio.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace partake::client::internal {

// Coalescing per-segment mapping cache, confined to the I/O thread (no
// locking). Holds mappings for the connection's lifetime; cleared in
// connection_impl::teardown() (mappings still referenced by live objviews
// survive via their own shared_ptr).
class segment_cache {
  public:
    using fetch_fn =
        std::function<asio::awaitable<segment_spec>(std::uint32_t)>;

    explicit segment_cache(fetch_fn fetch);

    // Cached mapping, or fetch spec -> attach -> cache -> resolve. Concurrent
    // callers for the same segment coalesce onto the in-flight attach. Throws
    // std::system_error on fetch/attach failure (matches request<>'s style).
    auto get(std::uint32_t segment_no)
        -> asio::awaitable<std::shared_ptr<mapping>>;

    void clear();

  private:
    struct entry {
        std::shared_ptr<mapping> map; // set once mapped
        bool pending = false;         // an attach is in flight
        std::vector<unique_handler<void(std::error_code)>> waiters;
    };

    fetch_fn fetch_;
    std::unordered_map<std::uint32_t, entry> entries_;
};

} // namespace partake::client::internal
