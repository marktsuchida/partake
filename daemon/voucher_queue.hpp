/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asio.hpp"
#include "time_point.hpp"

#include <boost/heap/fibonacci_heap.hpp>
#include <gsl/pointers>

#include <chrono>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace partake::daemon {

// Overridable traits to inject into voucher_queue. The time_point type is
// fixed as it needs to be globally consistent.
class steady_clock_traits {
    gsl::not_null<asio::io_context *> asio_context;

  public:
    using timer_type = asio::steady_timer;
    static_assert(std::is_same_v<clock, timer_type::clock_type>);

    explicit steady_clock_traits(asio::io_context &context)
        : asio_context(&context) {}

    static auto now() noexcept -> time_point { return clock::now(); }

    auto make_timer() -> timer_type {
        return asio::steady_timer(*asio_context);
    }

    auto make_timer(time_point tp) -> timer_type {
        return asio::steady_timer(*asio_context, tp);
    }
};

template <typename Object, typename ClockTraits = steady_clock_traits>
class voucher_queue {
  public:
    using object_type = Object; // May be incomplete type.
    using clock_traits_type = ClockTraits;

  private:
    using element_type = std::pair<time_point, std::shared_ptr<object_type>>;

    struct element_compare {
        auto operator()(element_type const &lhs,
                        element_type const &rhs) const noexcept {
            // Sort earlier time points to front of max-heap.
            return std::greater{}(lhs.first, rhs.first);
        }
    };

    // We use a Fibonacci heap (priority queue) for the voucher expiration
    // queue. It is ideal because it allows erasure of elements given a
    // pointer, and has good time complexity for our use case: insertion is
    // constant, deletion is amortized log N but in practice constant if no
    // pop-min has been performed since insertion. Pop-min is log N, but can be
    // avoided by apps that do not let vouchers expire.
    using queue_impl_type =
        boost::heap::fibonacci_heap<element_type,
                                    boost::heap::compare<element_compare>>;
    // TODO Consider using std::pmr::unsynchronized_pool_resource-based
    // allocator (but must measure to justify).

    queue_impl_type queue;
    gsl::not_null<clock_traits_type *> clk_traits;
    typename clock_traits_type::timer_type expiration_timer;
    time_point expiration_scheduled_time = time_point::max();

    // Extra delay when scheduling expiration task, to avoid waking up on every
    // voucher expiration.
    // TODO Make configurable.
    static constexpr auto expiration_extra_delay = std::chrono::seconds(1);

  public:
    using handle_type = typename queue_impl_type::handle_type;

    explicit voucher_queue(clock_traits_type &clock_traits)
        : clk_traits(&clock_traits),
          expiration_timer(clk_traits->make_timer()) {}

    // No move or copy (empty state not defined)
    auto operator=(voucher_queue &&) = delete;

    [[nodiscard]] auto empty() const noexcept -> bool { return queue.empty(); }

    void enqueue(std::shared_ptr<object_type> const &voucher) {
        auto exp = voucher->as_voucher().expiration();
        handle_type h = queue.push({exp, voucher});
        voucher->as_voucher().set_queued(h);
        schedule_expiration(exp);
    }

    void drop(std::shared_ptr<object_type> const &voucher) {
        auto h = voucher->as_voucher().clear_queued();
        if (h.has_value())
            queue.erase(h.value());
        // Let expiration timer reschedule (if necessary) when it activates.
    }

    void drop_all() {
        expiration_timer.cancel();
        expiration_scheduled_time = time_point::max();
        for (auto const &[e, v] : queue)
            v->as_voucher().clear_queued();
        queue.clear();
    }

  private:
    void drop_expired(time_point now) {
        // In theory we could also drop vouchers that are invalid for reasons
        // other than expiration. However, it would likely be slow to do that
        // here. Also, vouchers with no remaining count will have been removed
        // at the time of claim. The only vouchers that could remain are those
        // targetting objects that were subsequently unshared. At least for
        // now, we let such vouchers linger until they expire based on time,
        // because eagerly dropping them here would require scanning the whole
        // queue, which may be more work than we want to perform here.
        while (not queue.empty() && queue.top().first <= now) {
            auto const &voucher = queue.top().second;
            voucher->as_voucher().clear_queued();
            queue.pop();
        }
    }

    void schedule_expiration(time_point expiration) {
        if (expiration >= expiration_scheduled_time)
            return; // Already scheduled before given expiration.

        expiration_timer.cancel();

        expiration_scheduled_time = expiration + expiration_extra_delay;
        expiration_timer = clk_traits->make_timer(expiration_scheduled_time);
        expiration_timer.async_wait([this](boost::system::error_code err) {
            if (not err) {
                expiration_scheduled_time = time_point::max();
                drop_expired(clock_traits_type::now());
                if (not queue.empty())
                    schedule_expiration(queue.top().first);
            }
        });
    }
};

} // namespace partake::daemon
