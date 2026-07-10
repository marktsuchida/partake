/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "proper_object.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <trompeloeil/mock.hpp>

namespace partake::daemon {

namespace {

struct mock_handle : handle_list<mock_handle>::hook {
    // NOLINTNEXTLINE(modernize-use-trailing-return-type)
    MAKE_MOCK0(is_open_uniquely, auto()->bool);
    MAKE_MOCK0(resume_requests_pending_on_share, void());
    MAKE_MOCK0(resume_request_pending_on_unique_ownership, void());
};

} // namespace

TEST_CASE("proper_object") {
    // NOLINTBEGIN(readability-magic-numbers)
    proper_object<int, mock_handle> po(42);
    CHECK(po.resource() == 42);
    // NOLINTEND(readability-magic-numbers)
    CHECK_FALSE(po.is_opened_by_unique_handle());
    CHECK_FALSE(po.is_shared());
    CHECK(po.exclusive_writer() == nullptr);

    SECTION("open_once") {
        po.open();
        CHECK(po.is_opened_by_unique_handle());
        mock_handle h;

        po.close(&h);
    }

    SECTION("open_twice") {
        po.open();
        po.open();
        CHECK_FALSE(po.is_opened_by_unique_handle());
        mock_handle h;
        po.close(&h);
        po.close(&h);
    }

    SECTION("close_exclusive_writer") {
        po.open();
        mock_handle h;
        po.exclusive_writer(&h);

        SECTION("no_awaiting_share") {
            // Nothing special since h is not awaiting share
            po.close(&h);
        }

        SECTION("awaiting_share_self") {
            po.add_handle_awaiting_share(&h);
            // Handle must be unlinked before resumption, which may destroy it.
            REQUIRE_CALL(h, resume_requests_pending_on_share())
                .LR_SIDE_EFFECT(CHECK_FALSE(h.is_linked()))
                .TIMES(1);
            po.close(&h);
        }

        SECTION("awaiting_share_other") {
            mock_handle g;
            po.add_handle_awaiting_share(&g);
            REQUIRE_CALL(g, resume_requests_pending_on_share())
                .LR_SIDE_EFFECT(CHECK_FALSE(g.is_linked()))
                .TIMES(1);
            po.close(&h);
        }
    }

    SECTION("share") {
        po.open();
        mock_handle h;
        po.exclusive_writer(&h);

        mock_handle g;
        po.add_handle_awaiting_share(&g);
        REQUIRE_CALL(g, resume_requests_pending_on_share())
            .LR_SIDE_EFFECT(CHECK_FALSE(g.is_linked()))
            .TIMES(1);
        po.share();
        po.open();
        po.close(&g);
        po.close(&h);
    }

    SECTION("share_multiple_awaiting") {
        po.open();
        mock_handle h;
        po.exclusive_writer(&h);

        mock_handle g1;
        mock_handle g2;
        po.add_handle_awaiting_share(&g1);
        po.add_handle_awaiting_share(&g2);
        REQUIRE_CALL(g1, resume_requests_pending_on_share())
            .LR_SIDE_EFFECT(CHECK_FALSE(g1.is_linked()))
            .TIMES(1);
        REQUIRE_CALL(g2, resume_requests_pending_on_share())
            .LR_SIDE_EFFECT(CHECK_FALSE(g2.is_linked()))
            .TIMES(1);
        po.share();
        po.open();
        po.open();
        po.close(&g1);
        po.close(&g2);
        po.close(&h);
    }

    SECTION("unique_ownership") {
        po.open();
        mock_handle h;
        po.exclusive_writer(&h);
        po.share();

        po.open();
        mock_handle g;
        po.set_handle_awaiting_unique_ownership(&g);

        SECTION("close_awaiting") {
            REQUIRE_CALL(g, resume_request_pending_on_unique_ownership())
                .TIMES(1);
            ALLOW_CALL(g, is_open_uniquely()).RETURN(true);
            po.close(&g);
            po.close(&h);
        }

        SECTION("close_other") {
            REQUIRE_CALL(g, resume_request_pending_on_unique_ownership())
                .TIMES(1);
            ALLOW_CALL(g, is_open_uniquely()).RETURN(true);
            po.close(&h);
            po.unshare(&g);
            CHECK(po.exclusive_writer() == &g);
            po.close(&g);
        }
    }
}

} // namespace partake::daemon
