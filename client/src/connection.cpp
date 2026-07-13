/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/connection.hpp"

#include "connection_impl.hpp"
#include "event_impl.hpp"
#include "partake/event.hpp"
#include "partake/token.hpp"
#include "partake/types.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>

namespace partake::client {

namespace {

// Empty-handle use (anything but operator bool()) is a programmer error.
void require(std::shared_ptr<internal::connection_impl> const &impl) {
    if (not impl) {
        assert(false);
        std::terminate();
    }
}

} // namespace

connection::connection(
    std::shared_ptr<internal::connection_impl> impl) noexcept
    : impl_(std::move(impl)) {}

connection::operator bool() const noexcept { return impl_ != nullptr; }

auto connection::connection_number() const -> std::uint32_t {
    require(impl_);
    return impl_->connection_number();
}

auto connection::ping(void *user_data) -> op_id {
    require(impl_);
    internal::event_payload pl;
    pl.type = op_type::ping;
    pl.user_data = user_data;
    return impl_->submit_ping(std::move(pl));
}

auto connection::ping(completion c) -> op_id {
    require(impl_);
    internal::event_payload pl;
    pl.type = op_type::ping;
    pl.cont = std::move(c);
    return impl_->submit_ping(std::move(pl));
}

// In the stage-3/4 stubs below, the by-value 'completion' parameters are
// not yet moved anywhere (the real implementations will move them).
// NOLINTBEGIN(performance-unnecessary-value-param)

auto connection::alloc(std::uint64_t /* size */, alloc_options /* options */,
                       void * /* user_data */) -> op_id {
    require(impl_);
    // TODO(stage 3): alloc + objview.
    std::terminate();
}

auto connection::alloc(std::uint64_t /* size */, alloc_options /* options */,
                       completion /* c */) -> op_id {
    require(impl_);
    // TODO(stage 3): alloc + objview.
    std::terminate();
}

auto connection::open(token /* key */, open_options /* options */,
                      void * /* user_data */) -> op_id {
    require(impl_);
    // TODO(stage 3): open + objview.
    std::terminate();
}

auto connection::open(token /* key */, open_options /* options */,
                      completion /* c */) -> op_id {
    require(impl_);
    // TODO(stage 3): open + objview.
    std::terminate();
}

auto connection::create_voucher(token /* key */, std::uint32_t /* count */,
                                void * /* user_data */) -> op_id {
    require(impl_);
    // TODO(stage 4): vouchers.
    std::terminate();
}

auto connection::create_voucher(token /* key */, std::uint32_t /* count */,
                                completion /* c */) -> op_id {
    require(impl_);
    // TODO(stage 4): vouchers.
    std::terminate();
}

auto connection::discard_voucher(token /* key */, void * /* user_data */)
    -> op_id {
    require(impl_);
    // TODO(stage 4): vouchers.
    std::terminate();
}

auto connection::discard_voucher(token /* key */, completion /* c */)
    -> op_id {
    require(impl_);
    // TODO(stage 4): vouchers.
    std::terminate();
}

// NOLINTEND(performance-unnecessary-value-param)

auto connection::shutdown(void *user_data) -> op_id {
    require(impl_);
    internal::event_payload pl;
    pl.type = op_type::shutdown;
    pl.user_data = user_data;
    return impl_->submit_shutdown(std::move(pl));
}

auto connection::shutdown(completion c) -> op_id {
    require(impl_);
    internal::event_payload pl;
    pl.type = op_type::shutdown;
    pl.cont = std::move(c);
    return impl_->submit_shutdown(std::move(pl));
}

void connection::cancel(op_id id) {
    require(impl_);
    impl_->cancel(id);
}

namespace internal {

auto make_connection(std::shared_ptr<connection_impl> impl) noexcept
    -> connection {
    return connection(std::move(impl));
}

auto get_connection_impl(connection const &c) noexcept
    -> std::shared_ptr<connection_impl> const & {
    // Null for empty handles; callers must check.
    return c.impl_;
}

} // namespace internal

} // namespace partake::client
