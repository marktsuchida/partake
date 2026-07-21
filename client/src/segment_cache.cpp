/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "segment_cache.hpp"

#include "partake/errors.hpp"

#include "shmem_attach.hpp"

#include <utility>

namespace partake::client::internal {

segment_cache::segment_cache(fetch_fn fetch) : fetch_(std::move(fetch)) {}

auto segment_cache::get(std::uint32_t segment_no)
    -> asio::awaitable<std::shared_ptr<mapping>> {
    if (auto it = entries_.find(segment_no); it != entries_.end()) {
        if (it->second.map)
            co_return it->second.map;
        if (it->second.pending) {
            // Park until the in-flight attach resolves.
            auto token = asio::as_tuple(asio::use_awaitable);
            auto [ec] = co_await asio::async_initiate<decltype(token),
                                                      void(std::error_code)>(
                [this, segment_no](auto handler) {
                    entries_[segment_no].waiters.emplace_back(
                        unique_handler<void(std::error_code)>(
                            std::move(handler)));
                },
                token);
            if (auto woken = entries_.find(segment_no);
                woken != entries_.end() and woken->second.map)
                co_return woken->second.map;
            throw std::system_error(
                ec ? ec : make_error_code(client_errc::protocol_violation));
        }
    }

    // Absent (or a stale entry with neither map nor pending): drive the fetch.
    entries_[segment_no].pending = true;
    std::shared_ptr<mapping> result;
    std::error_code fail_ec;
    try {
        auto spec = co_await fetch_(segment_no);
        auto attached = attach(spec);
        if (attached)
            result = std::move(*attached);
        else
            fail_ec = attached.error();
    } catch (std::system_error const &ex) {
        fail_ec = ex.code();
    } catch (...) {
        fail_ec = make_error_code(client_errc::disconnected);
    }

    // Re-find: the map may have rehashed while suspended in fetch_. Collect
    // waiters before invoking them (their resumptions may re-enter get()).
    auto it = entries_.find(segment_no);
    std::vector<unique_handler<void(std::error_code)>> waiters =
        std::move(it->second.waiters);
    if (fail_ec) {
        entries_.erase(it); // Leave the segment retryable.
    } else {
        it->second.map = result;
        it->second.pending = false;
        it->second.waiters.clear();
    }
    for (auto &w : waiters)
        w(fail_ec);
    if (fail_ec)
        throw std::system_error(fail_ec);
    co_return result;
}

void segment_cache::clear() { entries_.clear(); }

} // namespace partake::client::internal
