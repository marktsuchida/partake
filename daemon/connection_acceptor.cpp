/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "connection_acceptor.hpp"

#include "testing.hpp"

#include <doctest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace partake::daemon {

using uds = asio::local::stream_protocol;

TEST_CASE("connection_acceptor: unstarted") {
    testing::tempdir const td;
    auto path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    asio::io_context ctx;
    connection_acceptor<uds> a(ctx, uds::endpoint(path.string()));
    CHECK_FALSE(std::filesystem::exists(path));
    a.close();
}

TEST_CASE("connection_acceptor: started") {
    testing::tempdir const td;
    auto path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    asio::io_context ctx;
    connection_acceptor<uds> a(ctx, uds::endpoint(path.string()));
    bool close_notified = false;
    a.start([](auto &&sock) { (void)sock; }, [&] { close_notified = true; });
    CHECK_FALSE(close_notified);
#ifdef _WIN32
    // std::filesystem::is_socket() doesn't work on Windows. (Also note that
    // std::filesystem::exists() throws when called on a socket.)
    DWORD const attrs = GetFileAttributesA(path.string().c_str());
    CHECK(attrs != INVALID_FILE_ATTRIBUTES);
    // Sockets have a reparse point (IO_REPARSE_TAG_AF_UNIX). Do a simple check
    // only here.
    bool looks_like_a_socket = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0u;
    CHECK(looks_like_a_socket);
#else
    CHECK(std::filesystem::is_socket(path));
#endif
    a.close();
    CHECK(close_notified);
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("connection_acceptor: connect") {
    testing::tempdir const td;
    auto path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    asio::io_context ctx;
    connection_acceptor<uds> a(ctx, uds::endpoint(path.string()));
    bool connection_detected = false;
    a.start(
        [&](auto &&sock) {
            (void)sock;
            connection_detected = true;
            a.close();
        },
        [] {});

    // Since we don't have C++20 latch:
    bool finished = false;
    std::mutex m;
    std::condition_variable cv;

    auto async = std::async(std::launch::async, [&] {
        asio::io_context ctx2;
        uds::socket client(ctx2);
        client.connect(uds::endpoint(path.string()));

        std::unique_lock lk(m);
        while (not finished) // NOLINT(bugprone-infinite-loop)
            cv.wait(lk);
    });

    CHECK_FALSE(connection_detected);
    ctx.run();
    CHECK(connection_detected);

    std::scoped_lock const lk(m);
    finished = true;
    cv.notify_one();
}

} // namespace partake::daemon
