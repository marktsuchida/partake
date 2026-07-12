/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/client.hpp"

#include "client_impl.hpp"
#include "partake/event.hpp"
#include "partake/queue.hpp"
#include "partake/types.hpp"

#include <cassert>
#include <exception>
#include <memory>
#include <string>

namespace partake::client {

client::client() : impl_(std::make_shared<internal::client_impl>()) {}

client::client(client_options const & /* options */)
    : impl_(std::make_shared<internal::client_impl>()) {}

client::~client() = default;

auto client::connect(std::string const & /* socket_path */,
                     std::string const & /* name */, queue & /* q */,
                     void * /* user_data */) -> op_id {
    // TODO(stage 2): hello-exchange connect coroutine.
    assert(false); // Not yet implemented.
    std::terminate();
}

auto client::connect(std::string const & /* socket_path */,
                     std::string const & /* name */, queue & /* q */,
                     completion /* c */) -> op_id {
    // TODO(stage 2): hello-exchange connect coroutine.
    assert(false); // Not yet implemented.
    std::terminate();
}

namespace internal {

auto get_client_impl(client const &c) noexcept
    -> std::shared_ptr<client_impl> const & {
    // Null for moved-from clients; callers must check.
    return c.impl_;
}

} // namespace internal

} // namespace partake::client
