/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "session.hpp"

#include "handle.hpp"
#include "key_sequence.hpp"
#include "object.hpp"
#include "repository.hpp"
#include "token.hpp"

#include <doctest.h>
#include <trompeloeil.hpp>

#include <chrono>

namespace partake::daemon {

// Partial integration tests using real object, handle, repository (but mock
// allocator, voucher_queue).

namespace {

struct mock_allocator {
    // Use 'int' as resource type.
    // NOLINTNEXTLINE(modernize-use-trailing-return-type)
    MAKE_MOCK1(allocate, auto(std::size_t)->int);
};

struct mock_segment {
    // Use 'int' as segment spec type.
    // NOLINTNEXTLINE(modernize-use-trailing-return-type)
    MAKE_CONST_MOCK0(spec, auto()->int);
};

struct mock_voucher_queue {
    using object_type = object<int>;
    MAKE_MOCK1(enqueue, void(std::shared_ptr<object_type>));
    MAKE_MOCK1(drop, void(std::shared_ptr<object_type>));
};

} // namespace

TEST_CASE("session: global ops") {
    using session_type =
        session<mock_allocator,
                repository<object<int>, key_sequence, mock_voucher_queue>,
                handle<object<int>>, mock_segment>;
    mock_allocator alloc;
    mock_segment const seg;
    mock_voucher_queue vq;
    repository<object<int>, key_sequence, mock_voucher_queue> repo(
        key_sequence(), vq);

    using protocol::Status;
    using namespace std::chrono_literals;

    session_type sess(42, seg, alloc, repo, 10s);
    CHECK(sess.is_valid());
    CHECK(sess.session_id() == 42);
    CHECK(sess.name().empty());
    CHECK(sess.pid() == 0);

    SUBCASE("hello") {
        std::uint32_t session_id = 0;
        sess.hello(
            "myclient", 1234, [&](std::uint32_t id) { session_id = id; },
            []([[maybe_unused]] Status err) { CHECK(false); });
        CHECK(session_id == 42);
        CHECK(sess.name() == "myclient");
        CHECK(sess.pid() == 1234);

        // Second call is error
        auto err = Status::OK;
        sess.hello(
            "", 0, [](std::uint32_t) { CHECK(false); },
            [&](Status e) { err = e; });
        CHECK(err == Status::INVALID_REQUEST);
    }

    SUBCASE("get_segment") {
        int spec = 0;
        ALLOW_CALL(seg, spec()).RETURN(5678);
        sess.get_segment(
            0, [&](int s) { spec = s; },
            []([[maybe_unused]] Status err) { CHECK(false); });
        CHECK(spec == 5678);
    }
}

TEST_CASE("session: object ops") {
    // Test operations on keys in various state. The overall plan is to test
    // each operation on a key in each state.

    using session_type =
        session<mock_allocator,
                repository<object<int>, key_sequence, mock_voucher_queue>,
                handle<object<int>>, mock_segment>;
    mock_allocator alloc;
    mock_segment const seg;
    mock_voucher_queue vq;
    repository<object<int>, key_sequence, mock_voucher_queue> repo(
        key_sequence(), vq);

    using common::token;
    using protocol::Policy;
    using protocol::Status;
    using trompeloeil::_;
    using namespace std::chrono_literals;

    session_type sess1(42, seg, alloc, repo, 10s);
    session_type sess2(43, seg, alloc, repo, 10s);

    GIVEN("nonexistent key") {
        std::vector const keys{token(0), token(12345)};
        for (token const key : keys) {
            CAPTURE(key);

            SUBCASE("open -> no such object") {
                std::vector const waits{false, true};
                std::vector const policies{Policy::DEFAULT, Policy::PRIMITIVE};
                for (bool wait : waits) {
                    CAPTURE(wait);
                    for (Policy policy : policies) {
                        CAPTURE(policy);

                        auto err = Status::OK;
                        sess1.open(
                            key, policy, wait, clock::now(),
                            []([[maybe_unused]] token k,
                               [[maybe_unused]] int r) { CHECK(false); },
                            [&](Status e) { err = e; },
                            []([[maybe_unused]] token k,
                               [[maybe_unused]] int r) { CHECK(false); },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                        CHECK(err == Status::NO_SUCH_OBJECT);
                    }
                }
            }

            SUBCASE("close -> no such object") {
                auto err = Status::OK;
                sess1.close(
                    key, [] { CHECK(false); }, [&](Status e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("share -> no such object") {
                auto err = Status::OK;
                sess1.share(
                    key, [] { CHECK(false); }, [&](Status e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("unshare -> no such object") {
                std::vector const waits{false, true};
                for (bool wait : waits) {
                    CAPTURE(wait);

                    auto err = Status::OK;
                    sess1.unshare(
                        key, wait,
                        []([[maybe_unused]] token k) { CHECK(false); },
                        [&](Status e) { err = e; },
                        []([[maybe_unused]] token k) { CHECK(false); },
                        []([[maybe_unused]] Status e) { CHECK(false); });
                    CHECK(err == Status::NO_SUCH_OBJECT);
                }
            }

            SUBCASE("create_voucher -> no such object") {
                auto err = Status::OK;
                sess1.create_voucher(
                    key, 1, clock::now(),
                    []([[maybe_unused]] token k) { CHECK(false); },
                    [&](Status e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("discard_voucher -> no such object") {
                auto err = Status::OK;
                sess1.discard_voucher(
                    key, clock::now(),
                    []([[maybe_unused]] token k) { CHECK(false); },
                    [&](Status e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }
    }

    GIVEN("default policy, unshared, opened by sess1") {
        token key;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](token k, int r) {
                CHECK(r == 532);
                key = k;
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(key.is_valid());

        SUBCASE("nil") {} // Test for correct cleanup when object left over.

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                key, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);

            SUBCASE("close by sess1 -> no such object") {
                auto err = Status::OK;
                sess1.close(
                    key, [] { CHECK(false); }, [&](Status e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }

        SUBCASE("close by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.close(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open-nowait by sess1 -> object busy") {
            auto err = Status::OK;
            sess1.open(
                key, Policy::DEFAULT, false, clock::now(),
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("open-nowait by sess2 -> object busy") {
            auto err = Status::OK;
            sess2.open(
                key, Policy::DEFAULT, false, clock::now(),
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("open-wait by sess1 -> waits") {
            token opened_key;
            auto err = Status::OK;
            sess1.open(
                key, Policy::DEFAULT, true, clock::now(),
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](token k, [[maybe_unused]] int r) { opened_key = k; },
                [&](Status e) { err = e; });
            CHECK_FALSE(opened_key.is_valid());
            CHECK(err == Status::OK);

            SUBCASE("close by sess1 -> open-wait fails, no such object") {
                bool ok = false;
                sess1.close(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("share by sess1 -> open-wait succeeds") {
                bool ok = false;
                sess1.share(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(opened_key == key);
            }
        }

        SUBCASE("open-wait by sess2 -> waits") {
            token opened_key;
            auto err = Status::OK;
            sess2.open(
                key, Policy::DEFAULT, true, clock::now(),
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](token k, [[maybe_unused]] int r) { opened_key = k; },
                [&](Status e) { err = e; });
            CHECK_FALSE(opened_key.is_valid());
            CHECK(err == Status::OK);

            SUBCASE("close by sess1 -> open-wait fails, no such object") {
                bool ok = false;
                sess1.close(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("share by sess1 -> open-wait succeeds") {
                bool ok = false;
                sess1.share(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(opened_key == key);
            }
        }

        SUBCASE("share by sess1 -> succeeds") {
            bool ok = false;
            sess1.share(
                key, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);
        }

        SUBCASE("share by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.share(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.unshare(
                key, true, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.unshare(
                key, true, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("create_voucher by sess1 -> succeeds") {
            token vkey;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess1.create_voucher(
                key, 1, clock::now(), [&](token k) { vkey = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vkey.is_valid());
            CHECK(vkey != key);
        }

        SUBCASE("create_voucher by sess2 -> succeeds") {
            token vkey;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess2.create_voucher(
                key, 1, clock::now(), [&](token k) { vkey = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vkey.is_valid());
            CHECK(vkey != key);
        }

        SUBCASE("discard_voucher by sess1 -> succeeds") {
            token ret;
            sess1.discard_voucher(
                key, clock::now(), [&](token k) { ret = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == key);

            SUBCASE("close by sess1 -> succeeds") {
                bool object_unaffected_by_discard_voucher = false;
                sess1.close(
                    key, [&] { object_unaffected_by_discard_voucher = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(object_unaffected_by_discard_voucher);
            }
        }

        SUBCASE("discard_voucher by sess2 -> succeeds") {
            token ret;
            sess2.discard_voucher(
                key, clock::now(), [&](token k) { ret = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == key);
        }
    }

    GIVEN("default policy, shared, opened by sess1") {
        token key;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](token k, int r) {
                CHECK(r == 532);
                sess1.share(
                    k, [&] { key = k; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(key.is_valid());

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                key, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);
        }

        SUBCASE("close by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.close(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open with wrong policy -> no such object") {
            auto err = Status::OK;
            sess1.open(
                key, Policy::PRIMITIVE, true, clock::now(),
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open-wait by sess1 -> succeeds") {
            token opened;
            sess1.open(
                key, Policy::DEFAULT, true, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);
        }

        SUBCASE("open-wait by sess2 -> succeeds") {
            token opened;
            sess2.open(
                key, Policy::DEFAULT, true, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);
        }

        SUBCASE("share by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.share(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("share by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.share(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-wait by sess1 -> succeeds") {
            token newkey;
            sess1.unshare(
                key, true, [&](token k) { newkey = k; },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(newkey.is_valid());
            CHECK(newkey != key);

            SUBCASE("open-wait by sess1 -> no such object") {
                auto err = Status::OK;
                sess1.open(
                    key, Policy::DEFAULT, true, clock::now(),
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    [&](Status e) { err = e; },
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }

        SUBCASE("unshare-wait by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.unshare(
                key, true, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("create_voucher by sess1 -> succeeds") {
            token vkey;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess1.create_voucher(
                key, 1, clock::now(), [&](token k) { vkey = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vkey.is_valid());
            CHECK(vkey != key);
        }

        SUBCASE("create_voucher by sess2 -> succeeds") {
            token vkey;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess2.create_voucher(
                key, 1, clock::now(), [&](token k) { vkey = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vkey.is_valid());
            CHECK(vkey != key);
        }

        SUBCASE("discard_voucher by sess1 -> succeeds") {
            token ret;
            sess1.discard_voucher(
                key, clock::now(), [&](token k) { ret = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == key);

            SUBCASE("close by sess1 -> succeeds") {
                bool object_unaffected_by_discard_voucher = false;
                sess1.close(
                    key, [&] { object_unaffected_by_discard_voucher = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(object_unaffected_by_discard_voucher);
            }
        }

        SUBCASE("discard_voucher by sess2 -> succeeds") {
            token ret;
            sess2.discard_voucher(
                key, clock::now(), [&](token k) { ret = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == key);
        }
    }

    GIVEN("default policy, shared, opened by sess1 and sess2") {
        token key;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](token k, int r) {
                CHECK(r == 532);
                sess1.share(
                    k,
                    [&] {
                        sess2.open(
                            k, Policy::DEFAULT, false, clock::now(),
                            [&](token k2, [[maybe_unused]] int r2) {
                                key = k2;
                            },
                            []([[maybe_unused]] Status e) { CHECK(false); },
                            []([[maybe_unused]] token k2,
                               [[maybe_unused]] int r2) { CHECK(false); },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(key.is_valid());

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                key, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);
        }

        SUBCASE("open-wait by sess1 -> succeeds") {
            token opened;
            sess1.open(
                key, Policy::DEFAULT, true, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);
        }

        SUBCASE("share by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.share(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-nowait by sess1 -> object busy") {
            auto err = Status::OK;
            sess1.unshare(
                key, false, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("unshare-wait by sess1 -> waits") {
            token newkey;
            auto err = Status::OK;
            sess1.unshare(
                key, true, []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](token k) { newkey = k; }, [&](Status e) { err = e; });
            CHECK_FALSE(newkey.is_valid());
            CHECK(err == Status::OK);

            SUBCASE("close by sess1 -> unshare fails, no such object") {
                bool ok = false;
                sess1.close(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK_FALSE(newkey.is_valid());
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("close by sess2 -> unshare succeeds") {
                bool ok = false;
                sess2.close(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(newkey.is_valid());
                CHECK(newkey != key);
                CHECK(err == Status::OK);
            }

            SUBCASE("unshare-wait by sess2 -> object reserved") {
                auto err2 = Status::OK;
                sess2.unshare(
                    key, true, []([[maybe_unused]] token k) { CHECK(false); },
                    [&](Status e) { err2 = e; },
                    []([[maybe_unused]] token k) { CHECK(false); },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(err2 == Status::OBJECT_RESERVED);
                CHECK_FALSE(newkey.is_valid()); // First unshare still pending.
                CHECK(err == Status::OK);
            }

            SUBCASE("open-wait by sess1 -> succeeds") {
                token opened;
                sess1.open(
                    key, Policy::DEFAULT, true, clock::now(),
                    [&](token k, int r) {
                        opened = k;
                        CHECK(r == 532);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); },
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(opened == key);
                CHECK_FALSE(newkey.is_valid()); // First unshare still pending.
                CHECK(err == Status::OK);
            }

            SUBCASE("open-wait by sess2 -> succeeds") {
                token opened;
                sess2.open(
                    key, Policy::DEFAULT, true, clock::now(),
                    [&](token k, int r) {
                        opened = k;
                        CHECK(r == 532);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); },
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(opened == key);
                CHECK_FALSE(newkey.is_valid()); // First unshare still pending.
                CHECK(err == Status::OK);
            }
        }
    }

    GIVEN("default policy, shared, opened by sess1 twice") {
        token key;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](token k, int r) {
                CHECK(r == 532);
                sess1.share(
                    k,
                    [&] {
                        sess1.open(
                            k, Policy::DEFAULT, false, clock::now(),
                            [&](token k2, [[maybe_unused]] int r2) {
                                key = k2;
                            },
                            []([[maybe_unused]] Status e) { CHECK(false); },
                            []([[maybe_unused]] token k2,
                               [[maybe_unused]] int r2) { CHECK(false); },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(key.is_valid());

        SUBCASE("unshare-nowait by sess1 -> object busy") {
            auto err = Status::OK;
            sess1.unshare(
                key, false, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("unshare-wait by sess1 -> waits") {
            token newkey;
            auto err = Status::OK;
            sess1.unshare(
                key, true, []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](token k) { newkey = k; }, [&](Status e) { err = e; });
            CHECK_FALSE(newkey.is_valid());
            CHECK(err == Status::OK);

            SUBCASE("close by sess1 -> unshare succeeds") {
                bool ok = false;
                sess1.close(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(newkey.is_valid());
                CHECK(newkey != key);
                CHECK(err == Status::OK);
            }
        }

        SUBCASE("open-wait by sess2 -> succeeds") {
            token opened;
            sess2.open(
                key, Policy::DEFAULT, true, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);

            SUBCASE("unshare-wait by sess2 -> waits") {
                token newkey;
                auto err = Status::OK;
                sess2.unshare(
                    key, true, []([[maybe_unused]] token k) { CHECK(false); },
                    []([[maybe_unused]] Status e) { CHECK(false); },
                    [&](token k) { newkey = k; }, [&](Status e) { err = e; });
                CHECK_FALSE(newkey.is_valid());
                CHECK(err == Status::OK);

                SUBCASE("close by sess1 -> unshare still waiting") {
                    bool ok = false;
                    sess1.close(
                        key, [&] { ok = true; },
                        []([[maybe_unused]] Status e) { CHECK(false); });
                    CHECK(ok);
                    CHECK_FALSE(newkey.is_valid());
                    CHECK(err == Status::OK);

                    SUBCASE("close by sess1 -> unshare succeeds") {
                        bool ok2 = false;
                        sess1.close(
                            key, [&] { ok2 = true; },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                        CHECK(ok2);
                        CHECK(newkey.is_valid());
                        CHECK(newkey != key);
                        CHECK(err == Status::OK);
                    }
                }
            }
        }
    }

    GIVEN("default policy, unshared, opened by sess1, and voucher") {
        token key;
        token vkey;
        // Keep voucher alive despite voucher queue being mocked:
        std::shared_ptr<object<int>> vptr;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        REQUIRE_CALL(vq, enqueue(_)).LR_SIDE_EFFECT(vptr = _1).TIMES(1);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](token k, int r) {
                key = k;
                CHECK(r == 532);
                sess1.create_voucher(
                    key, 1, clock::now(), [&](token k2) { vkey = k2; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(key.is_valid());
        CHECK(vkey.is_valid());
        CHECK(vkey != key);

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                key, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);

            SUBCASE("open-wait voucher by sess2 -> no such object") {
                auto err = Status::OK;
                REQUIRE_CALL(vq, drop(_))
                    .LR_SIDE_EFFECT(vptr.reset())
                    .TIMES(1);
                sess2.open(
                    vkey, Policy::DEFAULT, true, clock::now(),
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    [&](Status e) { err = e; },
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }

        SUBCASE("open-wait voucher by sess2 -> waits") {
            token opened;
            auto err = Status::OK;
            REQUIRE_CALL(vq, drop(_)).LR_SIDE_EFFECT(vptr.reset()).TIMES(1);
            sess2.open(
                vkey, Policy::DEFAULT, true, clock::now(),
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                [&](Status e) { err = e; });
            CHECK_FALSE(opened.is_valid());
            CHECK(err == Status::OK);

            SUBCASE("open-wait voucher by sess2 -> no such object") {
                auto err2 = Status::OK;
                sess2.open(
                    vkey, Policy::DEFAULT, true, clock::now(),
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    [&](Status e) { err2 = e; },
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(err2 == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("share by sess1 -> open succeeds") {
                bool ok = false;
                sess1.share(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(opened == key);
                CHECK(err == Status::OK);
            }

            SUBCASE("close by sess1 -> open fails, no such object") {
                bool ok = false;
                sess1.close(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK_FALSE(opened.is_valid());
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }

        SUBCASE("create_voucher voucher by sess1 -> succeeds") {
            token newvkey;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess1.create_voucher(
                vkey, 1, clock::now(), [&](token k) { newvkey = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(newvkey.is_valid());
            CHECK(newvkey != vkey);
        }

        SUBCASE("discard_voucher voucher by sess1 -> succeeds") {
            token ret;
            REQUIRE_CALL(vq, drop(_)).TIMES(1);
            sess1.discard_voucher(
                vkey, clock::now(), [&](token k) { ret = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == key);
        }

        SUBCASE("close voucher by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.close(
                vkey, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("share voucher by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.share(
                vkey, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-wait voucher by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.unshare(
                vkey, true, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }
    }

    GIVEN("default policy, shared, opened by sess1, and voucher") {
        token key;
        token vkey;
        // Keep voucher alive despite voucher queue being mocked:
        std::shared_ptr<object<int>> vptr;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        REQUIRE_CALL(vq, enqueue(_)).LR_SIDE_EFFECT(vptr = _1).TIMES(1);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](token k, int r) {
                key = k;
                CHECK(r == 532);
                sess1.share(
                    key,
                    [&] {
                        sess1.create_voucher(
                            key, 1, clock::now(), [&](token k2) { vkey = k2; },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(key.is_valid());
        CHECK(vkey.is_valid());
        CHECK(vkey != key);

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                key, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);

            SUBCASE("open-wait voucher by sess2 -> succeeds") {
                token opened;
                REQUIRE_CALL(vq, drop(_))
                    .LR_SIDE_EFFECT(vptr.reset())
                    .TIMES(1);
                sess2.open(
                    vkey, Policy::DEFAULT, true, clock::now(),
                    [&](token k, int r) {
                        opened = k;
                        CHECK(r == 532);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); },
                    []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(opened == key);
            }
        }

        SUBCASE("open-wait voucher by sess2 -> succeeds") {
            token opened;
            REQUIRE_CALL(vq, drop(_)).LR_SIDE_EFFECT(vptr.reset()).TIMES(1);
            sess2.open(
                vkey, Policy::DEFAULT, true, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);
        }

        SUBCASE("unshare-nowait by sess1 -> object busy") {
            auto err = Status::OK;
            sess1.unshare(
                key, false, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("unshare-wait by sess1 -> waits") {
            token newkey;
            auto err = Status::OK;
            sess1.unshare(
                key, true, []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](token k) { newkey = k; }, [&](Status e) { err = e; });
            CHECK_FALSE(newkey.is_valid());
            CHECK(err == Status::OK);

            SUBCASE("voucher expires -> unshare succeeds") {
                vptr.reset(); // Simulate expiration.
                CHECK(newkey.is_valid());
                CHECK(newkey != key);
                CHECK(err == Status::OK);
            }

            SUBCASE("close by sess1 -> unshare fails, no such object") {
                bool ok = false;
                sess1.close(
                    key, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK_FALSE(newkey.is_valid());
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }
    }

    GIVEN("primitive policy, opened by sess1") {
        token key;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::PRIMITIVE,
            [&](token k, int r) {
                CHECK(r == 532);
                key = k;
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(key.is_valid());

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                key, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);
        }

        SUBCASE("close by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.close(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open with wrong policy -> no such object") {
            auto err = Status::OK;
            sess1.open(
                key, Policy::DEFAULT, true, clock::now(),
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open-nowait by sess1 -> succeeds") {
            token opened;
            sess1.open(
                key, Policy::PRIMITIVE, false, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);
        }

        SUBCASE("open-nowait by sess2 -> succeeds") {
            token opened;
            sess2.open(
                key, Policy::PRIMITIVE, false, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);
        }

        SUBCASE("open-wait by sess1 -> succeeds") {
            token opened;
            sess1.open(
                key, Policy::PRIMITIVE, true, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);
        }

        SUBCASE("open-wait by sess2 -> succeeds") {
            token opened;
            sess2.open(
                key, Policy::PRIMITIVE, true, clock::now(),
                [&](token k, int r) {
                    opened = k;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] token k, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == key);
        }

        SUBCASE("share by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.share(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("share by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.share(
                key, [] { CHECK(false); }, [&](Status e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-wait by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.unshare(
                key, true, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-wait by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.unshare(
                key, true, []([[maybe_unused]] token k) { CHECK(false); },
                [&](Status e) { err = e; },
                []([[maybe_unused]] token k) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("create_voucher by sess1 -> succeeds") {
            token vkey;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess1.create_voucher(
                key, 1, clock::now(), [&](token k) { vkey = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vkey.is_valid());
            CHECK(vkey != key);
        }

        SUBCASE("create_voucher by sess2 -> succeeds") {
            token vkey;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess2.create_voucher(
                key, 1, clock::now(), [&](token k) { vkey = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vkey.is_valid());
            CHECK(vkey != key);
        }

        SUBCASE("discard_voucher by sess1 -> succeeds") {
            token ret;
            sess1.discard_voucher(
                key, clock::now(), [&](token k) { ret = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == key);
        }

        SUBCASE("discard_voucher by sess2 -> succeeds") {
            token ret;
            sess2.discard_voucher(
                key, clock::now(), [&](token k) { ret = k; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == key);
        }
    }

    // Prevent destroyed sessions from resuming requests (in other
    // sessions) awaiting unique ownership. This is required when
    // destroying sessions in a top-down manner.
    sess1.drop_pending_requests();
    sess2.drop_pending_requests();
}

} // namespace partake::daemon
