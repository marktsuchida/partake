/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef _WIN32
#error "Windows wakeup not implemented yet"
// TODO(win32): AF_UNIX SOCKET pair via bind+connect+accept under %TEMP%.
#endif

#include "partake/event.hpp"
#include "partake/types.hpp"

#include "posix.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace partake::client::internal {

// Shared state of a partake::client::queue.
//
// Coalescing invariant: with 'mut' held at entry and exit of any member
// function, exactly one wakeup byte is pending in the pipe iff 'events' is
// nonempty. All pipe reads and writes happen with 'mut' held. The byte is
// cleared only when the queue empties, so the wakeup handle stays readable
// after a partial drain. A closed queue is permanently empty and
// unsignaled: close() discards queued events and push() drops new ones
// (nobody can drain once the public handle is gone).
//
// Events are never delivered or destroyed while 'mut' is held: payload
// destructors and continuations may re-enter this queue (e.g. an objview
// auto-close submit on a stopped client falls back to push()). try_pop()
// hands the event out for the caller to deliver/destroy unlocked, drain()
// overwrites the caller's reused slots outside the lock, and close()
// discards outside the lock.
class queue_impl {
    std::mutex mut;
    std::deque<event> events; // Guarded by mut.
    bool closed = false;      // Guarded by mut.

    // Wakeup pipe; never moved after construction so that the read end's fd
    // (the public wakeup_handle) stays stable.
    common::posix::file_descriptor rd;
    common::posix::file_descriptor wr;

  public:
    // Throws std::system_error on failure to create the pipe.
    queue_impl() {
        std::array<int, 2> fds{};
        if (::pipe(fds.data()) != 0) {
            int const e = errno;
            throw std::system_error(e, std::generic_category(), "pipe");
        }
        // No pipe2() on macOS, so set the flags separately.
        for (int const fd : fds) {
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
            ::fcntl(fd, F_SETFL, O_NONBLOCK);
        }
        rd = common::posix::file_descriptor(fds[0]);
        wr = common::posix::file_descriptor(fds[1]);
    }

    ~queue_impl() = default;

    queue_impl(queue_impl const &) = delete;
    auto operator=(queue_impl const &) -> queue_impl & = delete;
    queue_impl(queue_impl &&) = delete;
    auto operator=(queue_impl &&) -> queue_impl & = delete;

    [[nodiscard]] auto wakeup() const noexcept -> wakeup_handle {
        return rd.get();
    }

    // Called when the public queue handle dies: nobody can drain anymore,
    // so discard queued events (breaking any ownership cycles through
    // their payloads) and drop future pushes.
    void close() noexcept {
        std::deque<event> discarded;
        {
            std::scoped_lock const lock(mut);
            closed = true;
            using std::swap;
            swap(discarded, events);
            clear_signal();
        }
        // 'discarded' is destroyed outside the lock: payloads may hold
        // handles (connection, later objview) whose release re-enters
        // (e.g. an objview auto-close submit).
    }

    // Callable from any thread.
    void push(event ev) {
        std::scoped_lock const lock(mut);
        if (closed)
            return;
        bool const was_empty = events.empty();
        events.push_back(std::move(ev));
        // Signaling inside the lock keeps the invariant exact and makes
        // wait_one()'s poll race-free.
        if (was_empty)
            signal();
    }

    // Pop one event if available; clears the wakeup signal when the pop
    // empties the queue. The returned event is delivered and destroyed by
    // the caller without 'mut' held.
    auto try_pop() -> std::optional<event> {
        std::scoped_lock const lock(mut);
        if (events.empty())
            return std::nullopt;
        auto ev = std::move(events.front());
        events.pop_front();
        if (events.empty())
            clear_signal();
        return ev;
    }

    auto size() -> std::size_t {
        std::scoped_lock const lock(mut);
        return events.size();
    }

    auto drain(event *out, std::size_t max_events) -> std::size_t {
        if (out == nullptr and max_events > 0) {
            assert(false);
            std::terminate();
        }
        std::span const dest(out, max_events);
        std::size_t n = 0;
        while (n < max_events) {
            auto ev = try_pop();
            if (not ev)
                break;
            // Assigned outside the lock: overwriting a reused slot may
            // destroy an old event whose payload destructor re-enters
            // this queue (cf. the close() comment).
            dest[n++] = std::move(*ev);
        }
        return n;
    }

    auto wait_one(std::chrono::milliseconds timeout) -> std::optional<event> {
        using std::chrono::ceil;
        using std::chrono::floor;
        using std::chrono::milliseconds;
        using std::chrono::steady_clock;
        auto const now = steady_clock::now();
        // Guard the addition below: converting to steady_clock's
        // (nanosecond) rep overflows for huge timeouts, so treat those as
        // infinite too.
        bool const infinite =
            timeout < milliseconds::zero() or
            timeout >
                floor<milliseconds>(steady_clock::time_point::max() - now);
        auto const deadline =
            infinite ? steady_clock::time_point::max() : now + timeout;
        for (;;) {
            if (auto ev = try_pop())
                return ev;
            int remaining = -1;
            if (not infinite) {
                auto const left =
                    ceil<milliseconds>(deadline - steady_clock::now());
                remaining = static_cast<int>(std::clamp<std::int64_t>(
                    left.count(), 0, std::numeric_limits<int>::max()));
            }
            struct pollfd pfd = {};
            pfd.fd = rd.get();
            pfd.events = POLLIN;
            int const r = ::poll(&pfd, 1, remaining);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                assert(false);
                std::terminate();
            }
            if (r == 0) { // Timed out; one final re-check.
                return try_pop();
            }
            // POLLIN: the poll does not consume the byte; a spurious
            // iteration (byte cleared by a racing drain) just loops.
        }
    }

  private:
    // Write the single wakeup byte ('mut' held; queue just became
    // nonempty).
    void signal() noexcept {
        char const b = 0;
        for (;;) {
            ssize_t const r = ::write(wr.get(), &b, 1);
            if (r == 1)
                return;
            if (r < 0 and errno == EINTR)
                continue;
            // EAGAIN is unreachable under the 0-or-1-byte invariant.
            assert(false);
            return;
        }
    }

    // Read the pipe dry ('mut' held; queue just became empty).
    void clear_signal() noexcept {
        std::array<char, 16> buf{};
        for (;;) {
            ssize_t const r = ::read(rd.get(), buf.data(), buf.size());
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN or errno == EWOULDBLOCK)
                    return;
                assert(false);
                return;
            }
            if (r == 0)
                return;
        }
    }
};

} // namespace partake::client::internal
