/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "session.hpp"

#include "handle.hpp"
#include "object.hpp"
#include "repository.hpp"
#include "token.hpp"

#include <doctest.h>
#include <trompeloeil.hpp>

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
                repository<object<int>, token_sequence, mock_voucher_queue>,
                handle<object<int>>, mock_segment>;
    mock_allocator alloc;
    mock_segment const seg;
    mock_voucher_queue vq;
    repository<object<int>, token_sequence, mock_voucher_queue> repo(
        token_sequence(), vq);

    session_type sess(42, seg, alloc, repo);
    CHECK(sess.is_valid());
    CHECK(sess.session_id() == 42);
    CHECK(sess.name().empty());
    CHECK(sess.pid() == 0);

    using protocol::Status;

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
            [&](auto e) { err = e; });
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
    // Test operations on tokens in various state. The overall plan is to test
    // each operation on a token in each state.

    using session_type =
        session<mock_allocator,
                repository<object<int>, token_sequence, mock_voucher_queue>,
                handle<object<int>>, mock_segment>;
    mock_allocator alloc;
    mock_segment const seg;
    mock_voucher_queue vq;
    repository<object<int>, token_sequence, mock_voucher_queue> repo(
        token_sequence(), vq);

    session_type sess1(42, seg, alloc, repo);
    session_type sess2(43, seg, alloc, repo);

    using protocol::Policy;
    using protocol::Status;
    using trompeloeil::_;

    GIVEN("nonexistent token") {
        std::vector<btoken> const tokens{0, 12345};
        for (btoken const tok : tokens) {
            CAPTURE(tok);

            SUBCASE("open -> no such object") {
                std::vector const waits{false, true};
                std::vector const policies{Policy::DEFAULT, Policy::PRIMITIVE};
                for (bool wait : waits) {
                    CAPTURE(wait);
                    for (Policy policy : policies) {
                        CAPTURE(policy);

                        auto err = Status::OK;
                        sess1.open(
                            tok, policy, wait, clock::now(),
                            []([[maybe_unused]] btoken t,
                               [[maybe_unused]] int r) { CHECK(false); },
                            [&](auto e) { err = e; },
                            []([[maybe_unused]] btoken t,
                               [[maybe_unused]] int r) { CHECK(false); },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                        CHECK(err == Status::NO_SUCH_OBJECT);
                    }
                }
            }

            SUBCASE("close -> no such object") {
                auto err = Status::OK;
                sess1.close(
                    tok, [] { CHECK(false); }, [&](auto e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("share -> no such object") {
                auto err = Status::OK;
                sess1.share(
                    tok, [] { CHECK(false); }, [&](auto e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("unshare -> no such object") {
                std::vector const waits{false, true};
                for (bool wait : waits) {
                    CAPTURE(wait);

                    auto err = Status::OK;
                    sess1.unshare(
                        tok, wait,
                        []([[maybe_unused]] btoken t) { CHECK(false); },
                        [&](auto e) { err = e; },
                        []([[maybe_unused]] btoken t) { CHECK(false); },
                        []([[maybe_unused]] Status e) { CHECK(false); });
                    CHECK(err == Status::NO_SUCH_OBJECT);
                }
            }

            SUBCASE("create_voucher -> no such object") {
                auto err = Status::OK;
                sess1.create_voucher(
                    tok, clock::now(),
                    []([[maybe_unused]] btoken t) { CHECK(false); },
                    [&](auto e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("discard_voucher -> no such object") {
                auto err = Status::OK;
                sess1.discard_voucher(
                    tok, clock::now(),
                    []([[maybe_unused]] btoken t) { CHECK(false); },
                    [&](auto e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }
    }

    GIVEN("default policy, unshared, opened by sess1") {
        btoken tok = 0;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](btoken t, int r) {
                CHECK(r == 532);
                tok = t;
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(tok != 0);

        SUBCASE("nil") {} // Test for correct cleanup when object left over.

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                tok, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);

            SUBCASE("close by sess1 -> no such object") {
                auto err = Status::OK;
                sess1.close(
                    tok, [] { CHECK(false); }, [&](auto e) { err = e; });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }

        SUBCASE("close by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.close(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open-nowait by sess1 -> object busy") {
            auto err = Status::OK;
            sess1.open(
                tok, Policy::DEFAULT, false, clock::now(),
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("open-nowait by sess2 -> object busy") {
            auto err = Status::OK;
            sess2.open(
                tok, Policy::DEFAULT, false, clock::now(),
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("open-wait by sess1 -> waits") {
            btoken opened_tok = 0;
            auto err = Status::OK;
            sess1.open(
                tok, Policy::DEFAULT, true, clock::now(),
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](btoken t, [[maybe_unused]] int r) { opened_tok = t; },
                [&](auto e) { err = e; });
            CHECK(opened_tok == 0);
            CHECK(err == Status::OK);

            SUBCASE("close by sess1 -> open-wait fails, no such object") {
                bool ok = false;
                sess1.close(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("share by sess1 -> open-wait succeeds") {
                bool ok = false;
                sess1.share(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(opened_tok == tok);
            }
        }

        SUBCASE("open-wait by sess2 -> waits") {
            btoken opened_tok = 0;
            auto err = Status::OK;
            sess2.open(
                tok, Policy::DEFAULT, true, clock::now(),
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](btoken t, [[maybe_unused]] int r) { opened_tok = t; },
                [&](auto e) { err = e; });
            CHECK(opened_tok == 0);
            CHECK(err == Status::OK);

            SUBCASE("close by sess1 -> open-wait fails, no such object") {
                bool ok = false;
                sess1.close(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("share by sess1 -> open-wait succeeds") {
                bool ok = false;
                sess1.share(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(opened_tok == tok);
            }
        }

        SUBCASE("share by sess1 -> succeeds") {
            bool ok = false;
            sess1.share(
                tok, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);
        }

        SUBCASE("share by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.share(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.unshare(
                tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.unshare(
                tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("create_voucher by sess1 -> succeeds") {
            btoken vtok = 0;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess1.create_voucher(
                tok, clock::now(), [&](btoken t) { vtok = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vtok != 0);
            CHECK(vtok != tok);
        }

        SUBCASE("create_voucher by sess2 -> succeeds") {
            btoken vtok = 0;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess2.create_voucher(
                tok, clock::now(), [&](btoken t) { vtok = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vtok != 0);
            CHECK(vtok != tok);
        }

        SUBCASE("discard_voucher by sess1 -> succeeds") {
            btoken ret = 0;
            sess1.discard_voucher(
                tok, clock::now(), [&](btoken t) { ret = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == tok);

            SUBCASE("close by sess1 -> succeeds") {
                bool object_unaffected_by_discard_voucher = false;
                sess1.close(
                    tok, [&] { object_unaffected_by_discard_voucher = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(object_unaffected_by_discard_voucher);
            }
        }

        SUBCASE("discard_voucher by sess2 -> succeeds") {
            btoken ret = 0;
            sess2.discard_voucher(
                tok, clock::now(), [&](btoken t) { ret = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == tok);
        }
    }

    GIVEN("default policy, shared, opened by sess1") {
        btoken tok = 0;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](btoken t, int r) {
                CHECK(r == 532);
                sess1.share(
                    t, [&] { tok = t; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(tok != 0);

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                tok, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);
        }

        SUBCASE("close by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.close(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open with wrong policy -> no such object") {
            auto err = Status::OK;
            sess1.open(
                tok, Policy::PRIMITIVE, true, clock::now(),
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open-wait by sess1 -> succeeds") {
            btoken opened = 0;
            sess1.open(
                tok, Policy::DEFAULT, true, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);
        }

        SUBCASE("open-wait by sess2 -> succeeds") {
            btoken opened = 0;
            sess2.open(
                tok, Policy::DEFAULT, true, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);
        }

        SUBCASE("share by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.share(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("share by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.share(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-wait by sess1 -> succeeds") {
            btoken newtok = 0;
            sess1.unshare(
                tok, true, [&](btoken t) { newtok = t; },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(newtok != 0);
            CHECK(newtok != tok);

            SUBCASE("open-wait by sess1 -> no such object") {
                auto err = Status::OK;
                sess1.open(
                    tok, Policy::DEFAULT, true, clock::now(),
                    []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    [&](auto e) { err = e; },
                    []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }

        SUBCASE("unshare-wait by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.unshare(
                tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("create_voucher by sess1 -> succeeds") {
            btoken vtok = 0;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess1.create_voucher(
                tok, clock::now(), [&](btoken t) { vtok = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vtok != 0);
            CHECK(vtok != tok);
        }

        SUBCASE("create_voucher by sess2 -> succeeds") {
            btoken vtok = 0;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess2.create_voucher(
                tok, clock::now(), [&](btoken t) { vtok = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vtok != 0);
            CHECK(vtok != tok);
        }

        SUBCASE("discard_voucher by sess1 -> succeeds") {
            btoken ret = 0;
            sess1.discard_voucher(
                tok, clock::now(), [&](btoken t) { ret = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == tok);

            SUBCASE("close by sess1 -> succeeds") {
                bool object_unaffected_by_discard_voucher = false;
                sess1.close(
                    tok, [&] { object_unaffected_by_discard_voucher = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(object_unaffected_by_discard_voucher);
            }
        }

        SUBCASE("discard_voucher by sess2 -> succeeds") {
            btoken ret = 0;
            sess2.discard_voucher(
                tok, clock::now(), [&](btoken t) { ret = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == tok);
        }
    }

    GIVEN("default policy, shared, opened by sess1 and sess2") {
        btoken tok = 0;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](btoken t, int r) {
                CHECK(r == 532);
                sess1.share(
                    t,
                    [&] {
                        sess2.open(
                            t, Policy::DEFAULT, false, clock::now(),
                            [&](btoken t2, [[maybe_unused]] int r2) {
                                tok = t2;
                            },
                            []([[maybe_unused]] Status e) { CHECK(false); },
                            []([[maybe_unused]] btoken t2,
                               [[maybe_unused]] int r2) { CHECK(false); },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(tok != 0);

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                tok, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);
        }

        SUBCASE("open-wait by sess1 -> succeeds") {
            btoken opened = 0;
            sess1.open(
                tok, Policy::DEFAULT, true, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);
        }

        SUBCASE("share by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.share(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-nowait by sess1 -> object busy") {
            auto err = Status::OK;
            sess1.unshare(
                tok, false, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("unshare-wait by sess1 -> waits") {
            btoken newtok = 0;
            auto err = Status::OK;
            sess1.unshare(
                tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](btoken t) { newtok = t; }, [&](auto e) { err = e; });
            CHECK(newtok == 0);
            CHECK(err == Status::OK);

            SUBCASE("close by sess1 -> unshare fails, no such object") {
                bool ok = false;
                sess1.close(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(newtok == 0);
                CHECK(err == Status::NO_SUCH_OBJECT);
            }

            SUBCASE("close by sess2 -> unshare succeeds") {
                bool ok = false;
                sess2.close(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(newtok != 0);
                CHECK(newtok != tok);
                CHECK(err == Status::OK);
            }

            SUBCASE("unshare-wait by sess2 -> object reserved") {
                auto err2 = Status::OK;
                sess2.unshare(
                    tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                    [&](auto e) { err2 = e; },
                    []([[maybe_unused]] btoken t) { CHECK(false); },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(err2 == Status::OBJECT_RESERVED);
                CHECK(newtok == 0); // First unshare still pending.
                CHECK(err == Status::OK);
            }

            SUBCASE("open-wait by sess1 -> succeeds") {
                btoken opened = 0;
                sess1.open(
                    tok, Policy::DEFAULT, true, clock::now(),
                    [&](btoken t, int r) {
                        opened = t;
                        CHECK(r == 532);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); },
                    []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(opened == tok);
                CHECK(newtok == 0); // First unshare still pending.
                CHECK(err == Status::OK);
            }

            SUBCASE("open-wait by sess2 -> succeeds") {
                btoken opened = 0;
                sess2.open(
                    tok, Policy::DEFAULT, true, clock::now(),
                    [&](btoken t, int r) {
                        opened = t;
                        CHECK(r == 532);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); },
                    []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(opened == tok);
                CHECK(newtok == 0); // First unshare still pending.
                CHECK(err == Status::OK);
            }
        }
    }

    GIVEN("default policy, shared, opened by sess1 twice") {
        btoken tok = 0;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](btoken t, int r) {
                CHECK(r == 532);
                sess1.share(
                    t,
                    [&] {
                        sess1.open(
                            t, Policy::DEFAULT, false, clock::now(),
                            [&](btoken t2, [[maybe_unused]] int r2) {
                                tok = t2;
                            },
                            []([[maybe_unused]] Status e) { CHECK(false); },
                            []([[maybe_unused]] btoken t2,
                               [[maybe_unused]] int r2) { CHECK(false); },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(tok != 0);

        SUBCASE("unshare-nowait by sess1 -> object busy") {
            auto err = Status::OK;
            sess1.unshare(
                tok, false, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("unshare-wait by sess1 -> waits") {
            btoken newtok = 0;
            auto err = Status::OK;
            sess1.unshare(
                tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](btoken t) { newtok = t; }, [&](auto e) { err = e; });
            CHECK(newtok == 0);
            CHECK(err == Status::OK);

            SUBCASE("close by sess1 -> unshare succeeds") {
                bool ok = false;
                sess1.close(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(newtok != 0);
                CHECK(newtok != tok);
                CHECK(err == Status::OK);
            }
        }

        SUBCASE("open-wait by sess2 -> succeeds") {
            btoken opened = 0;
            sess2.open(
                tok, Policy::DEFAULT, true, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);

            SUBCASE("unshare-wait by sess2 -> waits") {
                btoken newtok = 0;
                auto err = Status::OK;
                sess2.unshare(
                    tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                    []([[maybe_unused]] Status e) { CHECK(false); },
                    [&](btoken t) { newtok = t; }, [&](auto e) { err = e; });
                CHECK(newtok == 0);
                CHECK(err == Status::OK);

                SUBCASE("close by sess1 -> unshare still waiting") {
                    bool ok = false;
                    sess1.close(
                        tok, [&] { ok = true; },
                        []([[maybe_unused]] Status e) { CHECK(false); });
                    CHECK(ok);
                    CHECK(newtok == 0);
                    CHECK(err == Status::OK);

                    SUBCASE("close by sess1 -> unshare succeeds") {
                        bool ok2 = false;
                        sess1.close(
                            tok, [&] { ok2 = true; },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                        CHECK(ok2);
                        CHECK(newtok != 0);
                        CHECK(newtok != tok);
                        CHECK(err == Status::OK);
                    }
                }
            }
        }
    }

    GIVEN("default policy, unshared, opened by sess1, and voucher") {
        btoken tok = 0;
        btoken vtok = 0;
        // Keep voucher alive despite voucher queue being mocked:
        std::shared_ptr<object<int>> vptr;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        REQUIRE_CALL(vq, enqueue(_)).LR_SIDE_EFFECT(vptr = _1).TIMES(1);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](btoken t, int r) {
                tok = t;
                CHECK(r == 532);
                sess1.create_voucher(
                    tok, clock::now(), [&](btoken t2) { vtok = t2; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(tok != 0);
        CHECK(vtok != 0);
        CHECK(vtok != tok);

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                tok, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);

            SUBCASE("open-wait voucher by sess2 -> no such object") {
                auto err = Status::OK;
                REQUIRE_CALL(vq, drop(_))
                    .LR_SIDE_EFFECT(vptr.reset())
                    .TIMES(1);
                sess2.open(
                    vtok, Policy::DEFAULT, true, clock::now(),
                    []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    [&](auto e) { err = e; },
                    []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }

        SUBCASE("open-wait voucher by sess2 -> waits") {
            btoken opened = 0;
            auto err = Status::OK;
            REQUIRE_CALL(vq, drop(_)).LR_SIDE_EFFECT(vptr.reset()).TIMES(1);
            sess2.open(
                vtok, Policy::DEFAULT, true, clock::now(),
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                [&](auto e) { err = e; });
            CHECK(opened == 0);
            CHECK(err == Status::OK);

            SUBCASE("share by sess1 -> open succeeds") {
                bool ok = false;
                sess1.share(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(opened == tok);
                CHECK(err == Status::OK);
            }

            SUBCASE("close by sess1 -> open fails, no such object") {
                bool ok = false;
                sess1.close(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(opened == 0);
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }

        SUBCASE("create_voucher voucher by sess1 -> succeeds") {
            btoken newvtok = 0;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess1.create_voucher(
                vtok, clock::now(), [&](btoken t) { newvtok = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(newvtok != 0);
            CHECK(newvtok != vtok);
        }

        SUBCASE("discard_voucher voucher by sess1 -> succeeds") {
            btoken ret = 0;
            REQUIRE_CALL(vq, drop(_)).TIMES(1);
            sess1.discard_voucher(
                vtok, clock::now(), [&](btoken t) { ret = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == tok);
        }

        SUBCASE("close voucher by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.close(
                vtok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("share voucher by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.share(
                vtok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-wait voucher by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.unshare(
                vtok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }
    }

    GIVEN("default policy, shared, opened by sess1, and voucher") {
        btoken tok = 0;
        btoken vtok = 0;
        // Keep voucher alive despite voucher queue being mocked:
        std::shared_ptr<object<int>> vptr;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        REQUIRE_CALL(vq, enqueue(_)).LR_SIDE_EFFECT(vptr = _1).TIMES(1);
        sess1.alloc(
            1024, Policy::DEFAULT,
            [&](btoken t, int r) {
                tok = t;
                CHECK(r == 532);
                sess1.share(
                    tok,
                    [&] {
                        sess1.create_voucher(
                            tok, clock::now(), [&](btoken t2) { vtok = t2; },
                            []([[maybe_unused]] Status e) { CHECK(false); });
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(tok != 0);
        CHECK(vtok != 0);
        CHECK(vtok != tok);

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                tok, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);

            SUBCASE("open-wait voucher by sess2 -> succeeds") {
                btoken opened = 0;
                REQUIRE_CALL(vq, drop(_))
                    .LR_SIDE_EFFECT(vptr.reset())
                    .TIMES(1);
                sess2.open(
                    vtok, Policy::DEFAULT, true, clock::now(),
                    [&](btoken t, int r) {
                        opened = t;
                        CHECK(r == 532);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); },
                    []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                        CHECK(false);
                    },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(opened == tok);
            }
        }

        SUBCASE("open-wait voucher by sess2 -> succeeds") {
            btoken opened = 0;
            REQUIRE_CALL(vq, drop(_)).LR_SIDE_EFFECT(vptr.reset()).TIMES(1);
            sess2.open(
                vtok, Policy::DEFAULT, true, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);
        }

        SUBCASE("unshare-nowait by sess1 -> object busy") {
            auto err = Status::OK;
            sess1.unshare(
                tok, false, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::OBJECT_BUSY);
        }

        SUBCASE("unshare-wait by sess1 -> waits") {
            btoken newtok = 0;
            auto err = Status::OK;
            sess1.unshare(
                tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); },
                [&](btoken t) { newtok = t; }, [&](auto e) { err = e; });
            CHECK(newtok == 0);
            CHECK(err == Status::OK);

            SUBCASE("voucher expires -> unshare succeeds") {
                vptr.reset(); // Simulate expiration.
                CHECK(newtok != 0);
                CHECK(newtok != tok);
                CHECK(err == Status::OK);
            }

            SUBCASE("close by sess1 -> unshare fails, no such object") {
                bool ok = false;
                sess1.close(
                    tok, [&] { ok = true; },
                    []([[maybe_unused]] Status e) { CHECK(false); });
                CHECK(ok);
                CHECK(newtok == 0);
                CHECK(err == Status::NO_SUCH_OBJECT);
            }
        }
    }

    GIVEN("primitive policy, opened by sess1") {
        btoken tok = 0;
        REQUIRE_CALL(alloc, allocate(1024)).RETURN(532);
        sess1.alloc(
            1024, Policy::PRIMITIVE,
            [&](btoken t, int r) {
                CHECK(r == 532);
                tok = t;
            },
            []([[maybe_unused]] Status e) { CHECK(false); });
        CHECK(tok != 0);

        SUBCASE("close by sess1 -> succeeds") {
            bool ok = false;
            sess1.close(
                tok, [&] { ok = true; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ok);
        }

        SUBCASE("close by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.close(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open with wrong policy -> no such object") {
            auto err = Status::OK;
            sess1.open(
                tok, Policy::DEFAULT, true, clock::now(),
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("open-nowait by sess1 -> succeeds") {
            btoken opened = 0;
            sess1.open(
                tok, Policy::PRIMITIVE, false, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);
        }

        SUBCASE("open-nowait by sess2 -> succeeds") {
            btoken opened = 0;
            sess2.open(
                tok, Policy::PRIMITIVE, false, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);
        }

        SUBCASE("open-wait by sess1 -> succeeds") {
            btoken opened = 0;
            sess1.open(
                tok, Policy::PRIMITIVE, true, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);
        }

        SUBCASE("open-wait by sess2 -> succeeds") {
            btoken opened = 0;
            sess2.open(
                tok, Policy::PRIMITIVE, true, clock::now(),
                [&](btoken t, int r) {
                    opened = t;
                    CHECK(r == 532);
                },
                []([[maybe_unused]] Status e) { CHECK(false); },
                []([[maybe_unused]] btoken t, [[maybe_unused]] int r) {
                    CHECK(false);
                },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(opened == tok);
        }

        SUBCASE("share by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.share(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("share by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.share(
                tok, [] { CHECK(false); }, [&](auto e) { err = e; });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-wait by sess1 -> no such object") {
            auto err = Status::OK;
            sess1.unshare(
                tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("unshare-wait by sess2 -> no such object") {
            auto err = Status::OK;
            sess2.unshare(
                tok, true, []([[maybe_unused]] btoken t) { CHECK(false); },
                [&](auto e) { err = e; },
                []([[maybe_unused]] btoken t) { CHECK(false); },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(err == Status::NO_SUCH_OBJECT);
        }

        SUBCASE("create_voucher by sess1 -> succeeds") {
            btoken vtok = 0;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess1.create_voucher(
                tok, clock::now(), [&](btoken t) { vtok = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vtok != 0);
            CHECK(vtok != tok);
        }

        SUBCASE("create_voucher by sess2 -> succeeds") {
            btoken vtok = 0;
            REQUIRE_CALL(vq, enqueue(_)).TIMES(1);
            sess2.create_voucher(
                tok, clock::now(), [&](btoken t) { vtok = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(vtok != 0);
            CHECK(vtok != tok);
        }

        SUBCASE("discard_voucher by sess1 -> succeeds") {
            btoken ret = 0;
            sess1.discard_voucher(
                tok, clock::now(), [&](btoken t) { ret = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == tok);
        }

        SUBCASE("discard_voucher by sess2 -> succeeds") {
            btoken ret = 0;
            sess2.discard_voucher(
                tok, clock::now(), [&](btoken t) { ret = t; },
                []([[maybe_unused]] Status e) { CHECK(false); });
            CHECK(ret == tok);
        }
    }

    // Prevent destroyed sessions from resuming requests (in other sessions)
    // awaiting unique ownership. This is required when destroying sessions in
    // a top-down manner.
    sess1.drop_pending_requests();
    sess2.drop_pending_requests();
}

} // namespace partake::daemon
