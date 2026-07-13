/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "client_impl.hpp"

#include "connection_impl.hpp"

#include <utility>

namespace partake::client::internal {

void client_impl::quit() {
    {
        std::scoped_lock const lock(submit_mut);
        if (stopped)
            return;
        stopped = true;
        accepting = false;
    }
    // Posted after all try_post()-accepted submits (FIFO), so their ops are
    // registered (or their coroutines queued) before the quit fails them.
    asio::post(ctx, [this] {
        auto const conns = std::move(connections);
        for (auto const &conn : conns)
            conn->quit();
    });
    guard.reset();
    io_thread.join();
}

void client_impl::adopt_connection(std::shared_ptr<connection_impl> conn) {
    connections.insert(std::move(conn));
}

void client_impl::drop_connection(connection_impl *conn) {
    for (auto it = connections.begin(); it != connections.end(); ++it) {
        if (it->get() == conn) {
            connections.erase(it);
            return;
        }
    }
}

} // namespace partake::client::internal
