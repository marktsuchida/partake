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
    alloc_then_drop_everything(argv[1]);
    std::printf("ok: teardown with dropped handles did not hang\n");
    std::printf("PASS\n");
    return 0;
}
