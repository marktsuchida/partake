/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/client.hpp"

#include "client_impl.hpp"
#include "connection_impl.hpp"
#include "event_impl.hpp"
#include "partake/errors.hpp"
#include "partake/event.hpp"
#include "partake/queue.hpp"
#include "partake/types.hpp"
#include "queue_impl.hpp"

#include <cassert>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace partake::client {

namespace {

// Use of a moved-from client or queue is a programmer error.
void require(bool ok) {
    if (not ok) {
        assert(false);
        std::terminate();
    }
}

// The connect op is not currently cancelable: no connection handle exists to
// call cancel() on until the completion event arrives, so no op record is
// registered and run_connect pushes its event directly.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
auto do_connect(std::shared_ptr<internal::client_impl> const &impl,
                std::string const &socket_path, std::string const &name,
                queue &q, internal::event_payload pl) -> op_id {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    require(impl != nullptr);
    auto const &qimpl = internal::get_queue_impl(q);
    require(qimpl != nullptr);
    auto const id = impl->next_op_id();
    pl.id = id;
    pl.type = op_type::connect;
    auto conn = std::make_shared<internal::connection_impl>(impl, qimpl);
    bool const posted = impl->try_post([conn, path = socket_path, name = name,
                                        pl]() mutable {
        conn->start_connect(std::move(path), std::move(name), std::move(pl));
    });
    if (not posted) {
        // Lost the race with ~client; deliver from this thread.
        pl.error = make_error_code(client_errc::connect_failed);
        qimpl->push(internal::make_event(std::move(pl)));
    }
    return id;
}

} // namespace

client::client() : impl_(std::make_shared<internal::client_impl>()) {}

client::client(client_options const & /* options */)
    : impl_(std::make_shared<internal::client_impl>()) {}

client::~client() {
    if (impl_)
        impl_->quit();
}

auto client::operator=(client &&other) noexcept -> client & {
    if (this != &other) {
        if (impl_)
            impl_->quit();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

auto client::connect(std::string const &socket_path, std::string const &name,
                     queue &q, void *user_data) -> op_id {
    internal::event_payload pl;
    pl.user_data = user_data;
    return do_connect(impl_, socket_path, name, q, std::move(pl));
}

auto client::connect(std::string const &socket_path, std::string const &name,
                     queue &q, completion c) -> op_id {
    internal::event_payload pl;
    pl.cont = std::move(c);
    return do_connect(impl_, socket_path, name, q, std::move(pl));
}

namespace internal {

auto get_client_impl(client const &c) noexcept
    -> std::shared_ptr<client_impl> const & {
    // Null for moved-from clients; callers must check.
    return c.impl_;
}

} // namespace internal

} // namespace partake::client
