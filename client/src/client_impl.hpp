/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "partake/types.hpp"

#include "asio.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>

namespace partake::client::internal {

class connection_impl;

// Owns the background I/O thread. Pinned behind a shared_ptr; connections
// keep it alive so the io_context outlives them.
//
// Member order matters: 'ctx' precedes 'guard' (which refers to it), and
// 'io_thread' is last so that ctx.run() never sees a half-built object.
//
// quit() is the join point: it is called by client::~client() (and, as a
// safety net, by ~client_impl, which may run later, on any thread, when the
// last connection handle drops the final reference). Destruction of members
// therefore happens strictly after the I/O thread is joined.
class client_impl {
    asio::io_context ctx;
    asio::executor_work_guard<asio::io_context::executor_type> guard;
    std::atomic<op_id> next_op = 1; // 0 reserved: "no op".
    std::mutex submit_mut;
    bool accepting = true; // Guarded by submit_mut.
    bool stopped = false;  // Guarded by submit_mut.
    // Strong refs so that dropping public handles does not disconnect; see
    // the lifetime notes on connection_impl.
    std::unordered_set<std::shared_ptr<connection_impl>> connections;
    // I/O thread only.
    std::thread io_thread;

  public:
    client_impl()
        : guard(asio::make_work_guard(ctx)), io_thread([this] { ctx.run(); }) {
    }

    ~client_impl() { quit(); }

    client_impl(client_impl const &) = delete;
    auto operator=(client_impl const &) -> client_impl & = delete;
    client_impl(client_impl &&) = delete;
    auto operator=(client_impl &&) -> client_impl & = delete;

    [[nodiscard]] auto context() noexcept -> asio::io_context & { return ctx; }

    auto next_op_id() noexcept -> op_id {
        return next_op.fetch_add(1, std::memory_order_relaxed);
    }

    // Post to the I/O thread iff not stopped; returns false (without
    // posting) afterwards -- the caller must then deliver the op's terminal
    // event itself, from the submitting thread. The mutex closes the
    // check-then-post race with quit(): posts that won the race are queued
    // work, which ctx.run() executes (with everything they trigger) before
    // returning.
    auto try_post(std::function<void()> task) -> bool {
        std::scoped_lock const lock(submit_mut);
        if (not accepting)
            return false;
        asio::post(ctx, std::move(task));
        return true;
    }

    // Idempotent: reject new submits, quit live connections on the I/O thread
    // (failing their pending ops), reset the work guard, join.
    void quit();

    // I/O thread only:
    void adopt_connection(std::shared_ptr<connection_impl> conn);
    void drop_connection(connection_impl *conn);
};

} // namespace partake::client::internal
