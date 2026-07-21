/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/objview.hpp"

#include "event_impl.hpp"
#include "objview_impl.hpp"
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
void require(std::shared_ptr<internal::objview_impl> const &impl) {
    if (not impl) {
        assert(false);
        std::terminate();
    }
}

} // namespace

objview::objview(std::shared_ptr<internal::objview_impl> impl) noexcept
    : impl_(std::move(impl)) {}

objview::operator bool() const noexcept { return impl_ != nullptr; }

auto objview::key() const noexcept -> token {
    require(impl_);
    return impl_->key();
}

auto objview::size() const noexcept -> std::uint64_t {
    require(impl_);
    return impl_->size();
}

auto objview::writable() const noexcept -> bool {
    require(impl_);
    return impl_->writable();
}

auto objview::data() const noexcept -> void * {
    require(impl_);
    return impl_->data();
}

auto objview::close(void *user_data) -> op_id {
    require(impl_);
    internal::event_payload pl;
    pl.type = op_type::close;
    pl.user_data = user_data;
    return impl_->submit_close(std::move(pl));
}

auto objview::close(completion c) -> op_id {
    require(impl_);
    internal::event_payload pl;
    pl.type = op_type::close;
    pl.cont = std::move(c);
    return impl_->submit_close(std::move(pl));
}

auto objview::share(void * /* user_data */) -> op_id {
    require(impl_);
    // TODO(stage 4): share.
    std::terminate();
}

auto objview::share(completion /* c */) -> op_id {
    require(impl_);
    // TODO(stage 4): share.
    std::terminate();
}

auto objview::unshare(bool /* wait */, void * /* user_data */) -> op_id {
    require(impl_);
    // TODO(stage 4): unshare.
    std::terminate();
}

auto objview::unshare(bool /* wait */, completion /* c */) -> op_id {
    require(impl_);
    // TODO(stage 4): unshare.
    std::terminate();
}

auto objview::create_voucher(std::uint32_t /* count */, void * /* user_data */)
    -> op_id {
    require(impl_);
    // TODO(stage 4): vouchers.
    std::terminate();
}

auto objview::create_voucher(std::uint32_t /* count */, completion /* c */)
    -> op_id {
    require(impl_);
    // TODO(stage 4): vouchers.
    std::terminate();
}

namespace internal {

auto make_objview(std::shared_ptr<objview_impl> impl) noexcept -> objview {
    return objview(std::move(impl));
}

auto get_objview_impl(objview const &ov) noexcept
    -> std::shared_ptr<objview_impl> const & {
    // Null for empty handles; callers must check.
    return ov.impl_;
}

} // namespace internal

} // namespace partake::client
