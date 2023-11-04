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
    token k;
    protocol::Policy p = protocol::Policy::DEFAULT;
    int r = 0;
    std::size_t nv = 0;
    std::shared_ptr<mock_object> tgt;
    unsigned c = 0;
    time_point exp;

    explicit mock_object(token key, protocol::Policy policy, int resource)
        : v(false), k(key), p(policy), r(resource) {}

    explicit mock_object(token key, std::shared_ptr<mock_object> target,
                         unsigned count, time_point expiration)
        : v(true), k(key), tgt(std::move(target)), c(count), exp(expiration) {}

    ~mock_object() { CHECK(nv == 0); }

    void rekey(token key) { k = key; }

    auto key() const -> token { return k; }

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

struct mock_key_sequence { // Generate sequential starting at 1.
    std::uint64_t prev;

    auto generate() -> token { return token(++prev); }
};

struct mock_voucher_queue {
    using object_type = mock_object;

    MAKE_MOCK1(enqueue, void(std::shared_ptr<mock_object>));
    MAKE_MOCK1(drop, void(std::shared_ptr<mock_object>));
};

} // namespace

TEST_CASE("repository") {
    mock_voucher_queue vq;
    repository<mock_object, mock_key_sequence, mock_voucher_queue> r(
        mock_key_sequence(), vq);

    auto obj = r.create_object(protocol::Policy::DEFAULT, 42);
    CHECK(obj->key().as_u64() == 1);

    auto found = r.find_object(token(1));
    CHECK(found == obj);

    r.rekey_object(obj);
    CHECK(obj->key().as_u64() == 2);

    using trompeloeil::_;
    REQUIRE_CALL(vq, enqueue(_)).WITH(_1->key().as_u64() == 3).TIMES(1);
    auto v = r.create_voucher(obj, time_point(std::chrono::seconds(100)), 1);

    REQUIRE_CALL(vq, drop(_)).WITH(_1 == v).TIMES(1);
    CHECK(r.claim_voucher(v, time_point(std::chrono::seconds(50))));
    CHECK_FALSE(r.claim_voucher(v, time_point(std::chrono::seconds(50))));
}

} // namespace partake::daemon
