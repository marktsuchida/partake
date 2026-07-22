/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

// Manual end-to-end smoke test against a real partaked; deliberately built
// from the public headers only, and not registered as a meson test. Run:
//   partaked --socket <path> ... &
//   client_smoke <path>

#include <partake/partake.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using namespace partake::client;

auto wait_ok(queue &q, char const *what) -> event {
    auto ev = q.wait_one(std::chrono::milliseconds(5000));
    if (not ev.has_value()) {
        std::fprintf(stderr, "FAIL: timed out waiting for %s\n", what);
        std::exit(1);
    }
    if (ev->error()) {
        std::fprintf(stderr, "FAIL: %s: %s\n", what,
                     ev->error().message().c_str());
        std::exit(1);
    }
    std::printf("ok: %s\n", what);
    return *ev;
}

void alloc_write_close_shutdown(char const *path) {
    client c;
    queue q;
    (void)c.connect(path, "smoke", q, nullptr);
    auto conn = wait_ok(q, "connect").get_connection();

    (void)conn.alloc(1024, {}, nullptr);
    auto obj = wait_ok(q, "alloc").object();
    std::printf("     key %s, size %llu, data %p\n",
                obj.key().to_proquint().c_str(),
                static_cast<unsigned long long>(obj.size()), obj.data());
    std::memset(obj.data(), 0x5a, static_cast<std::size_t>(obj.size()));
    std::printf("ok: write through data()\n");

    (void)obj.close(nullptr);
    (void)wait_ok(q, "close");

    (void)conn.shutdown(nullptr);
    (void)wait_ok(q, "shutdown");
}

void check_bytes(objview const &obj, unsigned char value, char const *what) {
    auto const *p = static_cast<unsigned char const *>(obj.data());
    if (p == nullptr or p[0] != value or p[obj.size() - 1] != value) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
    std::printf("ok: %s\n", what);
}

void share_and_open_across_connections(char const *path) {
    client c;
    queue q;
    (void)c.connect(path, "smoke-a", q, nullptr);
    auto conn_a = wait_ok(q, "connect A").get_connection();

    (void)conn_a.alloc(1024, {}, nullptr);
    auto obj = wait_ok(q, "alloc on A").object();
    std::memset(obj.data(), 0x77, static_cast<std::size_t>(obj.size()));

    (void)obj.share(nullptr);
    auto shared = wait_ok(q, "share").object();
    if (shared.writable()) {
        std::fprintf(stderr, "FAIL: share sibling is writable\n");
        std::exit(1);
    }
    check_bytes(shared, 0x77, "share sibling is read-only, same bytes");

    (void)c.connect(path, "smoke-b", q, nullptr);
    auto conn_b = wait_ok(q, "connect B").get_connection();

    (void)conn_b.open(shared.key(), {}, nullptr);
    auto opened = wait_ok(q, "open from B").object();
    check_bytes(opened, 0x77, "bytes match across connections");

    (void)shared.create_voucher(2, nullptr);
    auto const vkey = wait_ok(q, "create_voucher on A").key();
    std::printf("     voucher %s\n", vkey.to_proquint().c_str());
    (void)conn_b.open(vkey, {}, nullptr);
    auto vopened = wait_ok(q, "open via voucher from B").object();
    check_bytes(vopened, 0x77, "voucher resolves to the same bytes");
    (void)vopened.close(nullptr);
    (void)wait_ok(q, "close voucher-opened view");
    (void)conn_a.discard_voucher(vkey, nullptr);
    (void)wait_ok(q, "discard_voucher");

    (void)opened.close(nullptr);
    (void)wait_ok(q, "close B's view");
    (void)shared.close(nullptr);
    (void)wait_ok(q, "close A's view");

    (void)conn_b.shutdown(nullptr);
    (void)wait_ok(q, "shutdown B");
    (void)conn_a.shutdown(nullptr);
    (void)wait_ok(q, "shutdown A");
}

void alloc_then_drop_everything(char const *path) {
    // No explicit close, no shutdown: ~objview must fire-and-forget a Close
    // and ~client must tear down without hanging.
    client c;
    queue q;
    (void)c.connect(path, "smoke-drop", q, nullptr);
    auto conn = wait_ok(q, "connect (drop sequence)").get_connection();
    (void)conn.alloc(1024, {}, nullptr);
    auto obj = wait_ok(q, "alloc (drop sequence)").object();
    std::memset(obj.data(), 0xa5, static_cast<std::size_t>(obj.size()));
}

} // namespace

auto main(int argc, char **argv) -> int {
    if (argc != 2) {
        std::fprintf(stderr, "usage: client_smoke <socket-path>\n");
        return 2;
    }
    alloc_write_close_shutdown(argv[1]);
    share_and_open_across_connections(argv[1]);
    alloc_then_drop_everything(argv[1]);
    std::printf("ok: teardown with dropped handles did not hang\n");
    std::printf("PASS\n");
    return 0;
}
