/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "partake/types.hpp"

#include "asio.hpp"

#include <atomic>
#include <thread>

namespace partake::client::internal {

// Owns the background I/O thread. Pinned behind a shared_ptr; connections
// keep it alive so the io_context outlives them.
//
// Member order matters: 'ctx' precedes 'guard' (which refers to it), and
// 'io_thread' is last so that ctx.run() never sees a half-built object.
// Destruction order: join (dtor body), then guard, then ctx. An exception
// escaping ctx.run() hits the thread terminate path (nothing posts work in
// this stage).
class client_impl {
    asio::io_context ctx;
    asio::executor_work_guard<asio::io_context::executor_type> guard;
    std::atomic<op_id> next_op = 1; // 0 reserved: "no op".
    std::thread io_thread;

  public:
    client_impl()
        : guard(asio::make_work_guard(ctx)), io_thread([this] { ctx.run(); }) {
    }

    ~client_impl() {
        // Stage 2 adds: post aborts of live connections.
        guard.reset();
        io_thread.join();
    }

    client_impl(client_impl const &) = delete;
    auto operator=(client_impl const &) -> client_impl & = delete;
    client_impl(client_impl &&) = delete;
    auto operator=(client_impl &&) -> client_impl & = delete;

    [[nodiscard]] auto context() noexcept -> asio::io_context & { return ctx; }

    auto next_op_id() noexcept -> op_id {
        return next_op.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace partake::client::internal
