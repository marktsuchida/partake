/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "voucher_queue.hpp"

#include <doctest.h>
#include <gsl/pointers>
#include <trompeloeil.hpp>

#include <functional>
#include <memory>
#include <optional>

namespace partake::daemon {

namespace {

struct mock_clock { // Helper to mock static now().
    static auto instance() -> mock_clock & {
        static mock_clock c;
        return c;
    }

    // NOLINTNEXTLINE(modernize-use-trailing-return-type)
    MAKE_MOCK0(now, auto()->time_point);
};

struct mock_timer_impl {
    MAKE_CONST_MOCK0(cancel, void());
    MAKE_CONST_MOCK1(async_wait,
                     void(std::function<void(boost::system::error_code err)>));
};

struct mock_timer {
    using clock_type = clock;

    std::shared_ptr<mock_timer_impl> impl;

    // NOLINTNEXTLINE(readability-make-member-function-const)
    void cancel() { impl->cancel(); }

    template <typename H> void async_wait(H h) { impl->async_wait(h); }
};

struct mock_clock_traits {
    using timer_type = mock_timer;

    static auto now() -> time_point { return mock_clock::instance().now(); }

    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MAKE_MOCK0(make_timer, auto()->timer_type);
    MAKE_MOCK1(make_timer, auto(time_point)->timer_type);
    // NOLINTEND(modernize-use-trailing-return-type)
};

struct mock_voucher {
    time_point exp;
    using queue_type = voucher_queue<mock_voucher, mock_clock_traits>;
    std::optional<typename queue_type::handle_type> h;

    explicit mock_voucher(time_point expiration) : exp(expiration) {}

    // Voucher is the same object in this mock.
    auto as_voucher() -> mock_voucher & { return *this; }

    [[nodiscard]] auto expiration() const -> time_point { return exp; }

    void set_queued(typename queue_type::handle_type qh) { h = qh; }

    auto clear_queued() -> std::optional<typename queue_type::handle_type> {
        auto ret = h;
        h.reset();
        return ret;
    }
};

} // namespace

TEST_CASE("voucher_queue") {
    using namespace std::chrono_literals;

    mock_clock_traits ct;
    auto empty_impl = std::make_shared<mock_timer_impl>();
    mock_timer const empty_timer{empty_impl};
    REQUIRE_CALL(ct, make_timer()).RETURN(empty_timer).TIMES(1);
    voucher_queue<mock_voucher, mock_clock_traits> vq(ct);
    CHECK(vq.empty());
    ALLOW_CALL(*empty_impl, cancel());
    vq.drop_all(); // Should be no-op on empty queue.

    auto v1 = std::make_shared<mock_voucher>(time_point(100s));

    using trompeloeil::_;

    SUBCASE("enqueue") {
        auto exp_impl = std::make_shared<mock_timer_impl>();
        mock_timer const exp_timer{exp_impl};
        REQUIRE_CALL(ct, make_timer(_))
            .WITH(_1 >= time_point(100s))
            .RETURN(exp_timer)
            .TIMES(1);
        std::function<void(boost::system::error_code)> handler;
        REQUIRE_CALL(*exp_impl, async_wait(_))
            .LR_SIDE_EFFECT(handler = _1)
            .TIMES(1);
        vq.enqueue(v1);
        CHECK(handler);
        CHECK_FALSE(vq.empty());
        CHECK(v1->h.has_value());

        SUBCASE("drop") {
            vq.drop(v1);
            CHECK(vq.empty());
            CHECK_FALSE(v1->h.has_value());
        }

        SUBCASE("fire; expired") {
            ALLOW_CALL(mock_clock::instance(), now()).RETURN(time_point(100s));
            handler({});
            CHECK(vq.empty());
            CHECK_FALSE(v1->h.has_value());
        }

        SUBCASE("fire; unexpired") {
            ALLOW_CALL(mock_clock::instance(), now()).RETURN(time_point(99s));
            ALLOW_CALL(*exp_impl, cancel());
            auto exp_impl2 = std::make_shared<mock_timer_impl>();
            mock_timer const exp_timer2{exp_impl2};
            REQUIRE_CALL(ct, make_timer(_))
                .WITH(_1 >= time_point(100s))
                .RETURN(exp_timer2)
                .TIMES(1);
            std::function<void(boost::system::error_code)> handler2;
            REQUIRE_CALL(*exp_impl2, async_wait(_))
                .LR_SIDE_EFFECT(handler2 = _1)
                .TIMES(1);
            handler({});
            CHECK(handler2);
            CHECK_FALSE(vq.empty());
            CHECK(v1->h.has_value());

            SUBCASE("fire; now expired") {
                ALLOW_CALL(mock_clock::instance(), now())
                    .RETURN(time_point(100s));
                handler({});
                CHECK(vq.empty());
                CHECK_FALSE(v1->h.has_value());
            }
        }
    }

    SUBCASE("enqueue already expired") {
        // Behavior is the same even if voucher is already expired when
        // enqueuing: expiration is deferred.
        auto exp_impl = std::make_shared<mock_timer_impl>();
        mock_timer const exp_timer{exp_impl};
        REQUIRE_CALL(ct, make_timer(_))
            .WITH(_1 >= time_point(100s))
            .RETURN(exp_timer)
            .TIMES(1);
        std::function<void(boost::system::error_code)> handler;
        REQUIRE_CALL(*exp_impl, async_wait(_))
            .LR_SIDE_EFFECT(handler = _1)
            .TIMES(1);
        vq.enqueue(v1);
        CHECK(handler);
        CHECK_FALSE(vq.empty());
        CHECK(v1->h.has_value());
    }
}

} // namespace partake::daemon
