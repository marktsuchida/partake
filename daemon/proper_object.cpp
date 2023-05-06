/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "proper_object.hpp"

#include <doctest.h>
#include <trompeloeil.hpp>

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
    proper_object<int, mock_handle> po(42);

    CHECK(po.resource() == 42);
    CHECK_FALSE(po.is_opened_by_unique_handle());
    CHECK_FALSE(po.is_shared());
    CHECK(po.exclusive_writer() == nullptr);

    SUBCASE("open_once") {
        po.open();
        CHECK(po.is_opened_by_unique_handle());
        mock_handle h;

        po.close(&h);
    }

    SUBCASE("open_twice") {
        po.open();
        po.open();
        CHECK_FALSE(po.is_opened_by_unique_handle());
        mock_handle h;
        po.close(&h);
        po.close(&h);
    }

    SUBCASE("close_exclusive_writer") {
        po.open();
        mock_handle h;
        po.exclusive_writer(&h);

        SUBCASE("no_awaiting_share") {
            // Nothing special since h is not awaiting share
            po.close(&h);
        }

        SUBCASE("awaiting_share_self") {
            po.add_handle_awaiting_share(&h);
            REQUIRE_CALL(h, resume_requests_pending_on_share()).TIMES(1);
            po.close(&h);
        }

        SUBCASE("awaiting_share_other") {
            mock_handle g;
            po.add_handle_awaiting_share(&g);
            REQUIRE_CALL(g, resume_requests_pending_on_share()).TIMES(1);
            po.close(&h);
        }
    }

    SUBCASE("share") {
        po.open();
        mock_handle h;
        po.exclusive_writer(&h);

        mock_handle g;
        po.add_handle_awaiting_share(&g);
        REQUIRE_CALL(g, resume_requests_pending_on_share()).TIMES(1);
        po.share();
        po.open();
        po.close(&g);
        po.close(&h);
    }

    SUBCASE("unique_ownership") {
        po.open();
        mock_handle h;
        po.exclusive_writer(&h);
        po.share();

        po.open();
        mock_handle g;
        po.set_handle_awaiting_unique_ownership(&g);

        SUBCASE("close_awaiting") {
            REQUIRE_CALL(g, resume_request_pending_on_unique_ownership())
                .TIMES(1);
            ALLOW_CALL(g, is_open_uniquely()).RETURN(true);
            po.close(&g);
            po.close(&h);
        }

        SUBCASE("close_other") {
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
