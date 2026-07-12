/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/objview.hpp"

#include "partake/event.hpp"
#include "partake/token.hpp"
#include "partake/types.hpp"

#include <cassert>
#include <cstdint>
#include <exception>

// Stage 1: objview_impl does not exist yet, so impl_ is always null and
// every member other than operator bool() terminates. The assert-on-empty
// is the final empty-handle behavior; stage 3 replaces the unconditional
// terminate with the real implementation.

namespace partake::client {

objview::operator bool() const noexcept { return impl_ != nullptr; }

auto objview::key() const noexcept -> token {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::size() const noexcept -> std::uint64_t {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::writable() const noexcept -> bool {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::data() const noexcept -> void * {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::close(void * /* user_data */) -> op_id {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::close(completion /* c */) -> op_id {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::share(void * /* user_data */) -> op_id {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::share(completion /* c */) -> op_id {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::unshare(bool /* wait */, void * /* user_data */) -> op_id {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::unshare(bool /* wait */, completion /* c */) -> op_id {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::create_voucher(std::uint32_t /* count */, void * /* user_data */)
    -> op_id {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

auto objview::create_voucher(std::uint32_t /* count */, completion /* c */)
    -> op_id {
    assert(impl_); // Empty-handle use is a programmer error.
    std::terminate();
}

} // namespace partake::client
