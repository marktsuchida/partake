/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "repository.hpp"

#include <doctest.h>
#include <trompeloeil.hpp>

namespace partake::daemon {

namespace {

struct mock_object : token_hash_table<mock_object>::hook,
                     std::enable_shared_from_this<mock_object> {
    bool v;
    btoken t = 0;
    protocol::Policy p = protocol::Policy::REGULAR;
    int r = 0;
    std::size_t nv = 0;
    std::shared_ptr<mock_object> tgt;
    unsigned c = 0;
    time_point exp;

    explicit mock_object(btoken tok, protocol::Policy policy, int resource)
        : v(false), t(tok), p(policy), r(resource) {}

    explicit mock_object(btoken tok, std::shared_ptr<mock_object> target,
                         unsigned count, time_point expiration)
        : v(true), t(tok), tgt(std::move(target)), c(count), exp(expiration) {}

    ~mock_object() { CHECK(nv == 0); }

    void reassign_token(btoken tok) { t = tok; }

    auto token() const -> btoken { return t; }

    auto is_proper_object() const -> bool { return not v; }

    auto as_proper_object() -> mock_object & { return *this; }

    void add_voucher() { ++nv; }

    void drop_voucher() {
        CHECK(nv > 0);
        --nv;
    }

    auto is_voucher() const -> bool { return v; }

    auto as_voucher() -> mock_object & { return *this; }

    auto claim(time_point now) -> bool {
        if (is_valid(now)) {
            --c;
            return true;
        }
        return false;
    }

    auto is_valid(time_point now) const -> bool { return c > 0 && now <= exp; }

    // NOLINTNEXTLINE(readability-make-member-function-const)
    auto target() -> std::shared_ptr<mock_object> { return tgt; }
};

struct mock_token_sequence { // Generate sequential starting at 1.
    btoken prev_token = 0;

    auto generate() -> btoken { return ++prev_token; }
};

struct mock_voucher_queue {
    using object_type = mock_object;

    MAKE_MOCK1(enqueue, void(std::shared_ptr<mock_object>));
    MAKE_MOCK1(drop, void(std::shared_ptr<mock_object>));
};

} // namespace

TEST_CASE("repository") {
    mock_voucher_queue vq;
    repository<mock_object, mock_token_sequence, mock_voucher_queue> r(
        mock_token_sequence(), vq);

    auto obj = r.create_object(protocol::Policy::REGULAR, 42);
    CHECK(obj->token() == 1);

    auto found = r.find_object(1);
    CHECK(found == obj);

    r.reassign_object_token(obj);
    CHECK(obj->token() == 2);

    using trompeloeil::_;
    REQUIRE_CALL(vq, enqueue(_)).WITH(_1->token() == 3).TIMES(1);
    auto v = r.create_voucher(obj, time_point(std::chrono::seconds(100)), 1);

    REQUIRE_CALL(vq, drop(_)).WITH(_1 == v).TIMES(1);
    CHECK(r.claim_voucher(v, time_point(std::chrono::seconds(50))));
    CHECK_FALSE(r.claim_voucher(v, time_point(std::chrono::seconds(50))));
}

} // namespace partake::daemon
