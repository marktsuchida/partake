/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "objview_impl.hpp"

#include "connection_impl.hpp"

#include <utility>

namespace partake::client::internal {

namespace {

// For now we always map in performance mode.
performance_mapping_policy const the_mapping_policy;

} // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
objview_impl::objview_impl(std::shared_ptr<connection_impl> connection,
                           std::shared_ptr<mapping> segment_mapping,
                           std::uint64_t offset, std::uint64_t size,
                           bool writable, token key)
    : conn(std::move(connection)), map(std::move(segment_mapping)),
      offset_(offset), size_(size), writable_(writable), key_(key) {}
// NOLINTEND(bugprone-easily-swappable-parameters)

objview_impl::~objview_impl() {
    if (st.load() != obj_state::open)
        return;
    event_payload pl;
    pl.type = op_type::close;
    pl.suppress = true;
    (void)conn->submit_close(key_, nullptr, std::move(pl));
}

auto objview_impl::data() const noexcept -> void * {
    if (st.load() != obj_state::open)
        return nullptr;
    return the_mapping_policy.object_data(*map, offset_);
}

auto objview_impl::submit_close(event_payload pl) -> op_id {
    return conn->submit_close(key_, shared_from_this(), std::move(pl));
}

auto objview_impl::submit_share(event_payload pl) -> op_id {
    return conn->submit_share(shared_from_this(), std::move(pl));
}

auto objview_impl::submit_unshare(bool wait, event_payload pl) -> op_id {
    return conn->submit_unshare(shared_from_this(), wait, std::move(pl));
}

auto objview_impl::submit_create_voucher(std::uint32_t count, event_payload pl)
    -> op_id {
    return conn->submit_create_voucher(key_, count, shared_from_this(),
                                       std::move(pl));
}

auto objview_impl::make_sibling(bool writable, token new_key)
    -> std::shared_ptr<objview_impl> {
    return std::make_shared<objview_impl>(conn, map, offset_, size_, writable,
                                          new_key);
}

auto objview_impl::begin_close() -> bool {
    auto expected = obj_state::open;
    return st.compare_exchange_strong(expected, obj_state::closing);
}

void objview_impl::mark_closed() { st.store(obj_state::closed); }

void objview_impl::revert_close() { st.store(obj_state::open); }

} // namespace partake::client::internal
