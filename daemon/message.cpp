/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "message.hpp"

#include "asio.hpp"
#include "posix.hpp"
#include "testing.hpp"
#include "win32.hpp"

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace partake::daemon {

// Testing async_message_writer and async_message_reader is slightly
// complicated due to platform differences in Asio support.
//
// In our production code, we use these class templates with Unix domain
// sockets. For testing, it is more convenient to use a pair of connected
// pipes, or a regular file. However, these have the following limitations:
//
// - Pipes (connected pair of asio::readable_pipe and asio::writable_pipe) give
//   the wrong error code at the end of the stream on Windows
//   (asio::error::broken_pipe instead of asio::error::eof). Therefore they
//   cannot be used to test correct behavior or async_message_reader if the EOF
//   will be handled.
//
// - At least under some Linux configurations (Ubuntu 22.04), pipes are not
//   available in Asio.
//
// - File support is platform-dependent. On Windows, asio::stream_file can be
//   used. On POSIX systems, asio::posix::stream_descriptor can be used. At
//   least on macOS, asio::stream_file is not available.
//
// Below, we first confirm that the EOF behavior of Unix domain sockets is as
// expected (in contrast to pipes): asio::error::eof is received at the end of
// the stream. Then we use files for actual testing.

TEST_CASE("Unix domain socket stream finishes with asio::error::eof") {
    // This also serves as a brief summary of how to use
    // asio::local::stream_protocol server and client.

#ifdef _WIN32
    using unlinkable_type = typename win32::unlinkable;
#else
    using unlinkable_type = typename posix::unlinkable;
#endif

    testing::tempdir const td;
    auto path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    asio::local::stream_protocol::endpoint const endpt(path.string());

    asio::io_context ctx;
    bool test_finished = false;

    // Server accepts 1 connection, reads, and closes.
    asio::local::stream_protocol::acceptor server(ctx);
    boost::system::error_code ec;
    server.open(endpt.protocol(), ec);
    CHECK_FALSE(ec);
    server.bind(endpt, ec);
    CHECK_FALSE(ec);
    unlinkable_type const unlk(path.string());
    server.listen(asio::local::stream_protocol::socket::max_listen_connections,
                  ec);
    CHECK_FALSE(ec);
    asio::local::stream_protocol::socket sock(ctx);
    std::vector<std::uint8_t> received;
    server.async_accept(sock, [&](boost::system::error_code ec2) {
        CHECK_FALSE(ec2);
        asio::async_read(sock, asio::dynamic_buffer(received),
                         [&](boost::system::error_code ec3, std::size_t read) {
                             CHECK(ec3 == asio::error::eof); // The test!
                             CHECK(read == 8);
                             CHECK(received.size() == 8);
                             CHECK(received[0] == 'x');
                             CHECK(received[6] == 'x');
                             CHECK(received[7] == '\0');
                             sock.close();
                             server.close();
                             test_finished = true;
                         });
    });

    // Client connects, writes, and closes.
    // Write 7 + 1 bytes to test for correct behavior on Windows (see writer
    // tests below).
    asio::local::stream_protocol::socket client(ctx);
    std::vector<std::uint8_t> data(7, 'x');
    std::vector<std::uint8_t> pad(1, '\0');
    client.async_connect(endpt, [&](boost::system::error_code ec2) {
        CHECK_FALSE(ec2);
        asio::async_write(
            client, std::array{asio::buffer(data), asio::buffer(pad)},
            [&](boost::system::error_code ec3, std::size_t written) {
                CHECK_FALSE(ec3);
                CHECK(written == 8);
                client.close();
            });
    });

    ctx.run();
    CHECK(test_finished);
}

namespace {

[[maybe_unused]] inline auto
writable_asio_stream_for_file(asio::io_context &ctx,
                              std::filesystem::path const &path) {

#ifdef _WIN32
    return asio::stream_file(ctx, path.string(),
                             asio::stream_file::write_only |
                                 asio::stream_file::create |
                                 asio::stream_file::exclusive);
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    auto fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    return asio::posix::stream_descriptor(ctx, fd);
#endif
}

[[maybe_unused]] inline auto
get_file_contents(std::filesystem::path const &path) {
    std::ifstream s(path, std::ios::binary);
    std::vector<std::uint8_t> buf;
    buf.resize(1024); // Enough for test.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    s.read(reinterpret_cast<char *>(buf.data()),
           static_cast<std::streamsize>(buf.size()));
    CHECK(s.eof());
    buf.resize(static_cast<std::size_t>(s.gcount()));
    return buf;
}

[[maybe_unused]] inline auto
readable_asio_stream_for_file(asio::io_context &ctx,
                              std::filesystem::path const &path) {
#ifdef _WIN32
    return asio::stream_file(ctx, path.string(), asio::stream_file::read_only);
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    return asio::posix::stream_descriptor(ctx, ::open(path.c_str(), O_RDONLY));
#endif
}

} // namespace

TEST_CASE("async_message_writer") {
    testing::tempdir const td;
    auto path = testing::unique_path(
        td.path(), testing::make_test_filename(__FILE__, __LINE__));
    asio::io_context ctx;
    auto s = writable_asio_stream_for_file(ctx, path);
    testing::auto_delete_file const adf(path);

    bool done = false;
    async_message_writer<decltype(s), std::vector<std::uint8_t>> writer(
        s, [&](std::error_code e) {
            CHECK_FALSE(e);
            done = true;
            s.close();
        });

    SUBCASE("empty") {
        std::vector<std::uint8_t> v;
        writer.async_write_message(std::move(v));
        ctx.run();
        CHECK(done);
        auto data = get_file_contents(path);
        CHECK(data.empty());
    }

    SUBCASE("unaligned-7") {
        std::vector<std::uint8_t> v{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
        v.resize(7); // Chop off 'h'
        writer.async_write_message(std::move(v));
        ctx.run();
        CHECK(done);
        auto data = get_file_contents(path);
#ifdef _WIN32
        // Windows asio::stream_file appears to add an extra byte to the file,
        // despite reporting having written 8 bytes. Let's assume for now that
        // this is an artifact of stream_file only. We check that this issue
        // does not arise in the Unix domain socket test above.
        while (data.size() > 8)
            data.pop_back();
#endif
        CHECK(data == std::vector<std::uint8_t>{'a', 'b', 'c', 'd', 'e', 'f',
                                                'g', '\0'});
    }

    SUBCASE("unaligned-9") {
        std::vector<std::uint8_t> v(9, 'a');
        writer.async_write_message(std::move(v));
        ctx.run();
        CHECK(done);
        auto data = get_file_contents(path);
#ifdef _WIN32
        // Windows asio::stream_file appears to add extra bytes to the file,
        // despite reporting having written 16 bytes. Let's assume for now that
        // this is an artifact of stream_file only.
        while (data.size() > 16)
            data.pop_back();
#endif
        std::vector<std::uint8_t> expected(9, 'a');
        std::vector<std::uint8_t> const zeroes(7, '\0');
        std::copy(zeroes.begin(), zeroes.end(), std::back_inserter(expected));
        CHECK(data == expected);
    }

    SUBCASE("aligned") {
        std::vector<std::uint8_t> v{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
        writer.async_write_message(std::move(v));
        ctx.run();
        CHECK(done);
        auto data = get_file_contents(path);
        CHECK(data == std::vector<std::uint8_t>{'a', 'b', 'c', 'd', 'e', 'f',
                                                'g', 'h'});
    }
}

TEST_CASE("async_message_reader: empty stream") {
    testing::tempdir const td;
    auto f = testing::unique_file_with_data(
        td.path(), testing::make_test_filename(__FILE__, __LINE__), {});
    asio::io_context ctx;
    auto s = readable_asio_stream_for_file(ctx, f.path());

    bool ended = false;
    async_message_reader r(
        s,
        [](gsl::span<std::uint8_t const> msg) -> bool {
            (void)msg;
            CHECK(false); // Should not be called
            return false;
        },
        [&](std::error_code ec) {
            CHECK_FALSE(ec);
            ended = true;
        });
    r.start();
    ctx.run();
    CHECK(ended);
}

TEST_CASE("async_message_reader: single empty message") {
    // Single message with size header 0, padded to 8 bytes
    std::vector<std::uint8_t> v(8, 0);

    testing::tempdir const td;
    auto f = testing::unique_file_with_data(
        td.path(), testing::make_test_filename(__FILE__, __LINE__), v);
    asio::io_context ctx;
    auto s = readable_asio_stream_for_file(ctx, f.path());

    bool received = false;
    async_message_reader r(
        s,
        [&](gsl::span<std::uint8_t const> msg) -> bool {
            CHECK(msg.size() == 8);
            CHECK(msg[0] == 0);
            CHECK(msg[7] == 0);
            received = true;
            return false;
        },
        [&](std::error_code ec) { CHECK_FALSE(ec); });
    r.start();
    ctx.run();
    CHECK(received);
}

TEST_CASE("async_message_reader: large message") {
    std::vector<std::uint8_t> v{0xfc, 0x7f, 0, 0}; // 32764 in little-endian
    v.resize(32768);
    v[32767] = 42;

    testing::tempdir const td;
    auto f = testing::unique_file_with_data(
        td.path(), testing::make_test_filename(__FILE__, __LINE__), v);
    asio::io_context ctx;
    auto s = readable_asio_stream_for_file(ctx, f.path());

    bool received = false;
    async_message_reader r(
        s,
        [&](gsl::span<std::uint8_t const> msg) -> bool {
            CHECK(msg.size() == 32768);
            CHECK(msg[32767] == 42);
            CHECK_FALSE(received);
            received = true;
            return false;
        },
        [&](std::error_code ec) { CHECK_FALSE(ec); });
    r.start();
    ctx.run();
    CHECK(received);
}

TEST_CASE("async_message_reader: quit by handler") {
    // Two messages with size header 0, padded to 8 bytes each
    std::vector<std::uint8_t> v(16, 0);

    testing::tempdir const td;
    auto f = testing::unique_file_with_data(
        td.path(), testing::make_test_filename(__FILE__, __LINE__), v);
    asio::io_context ctx;
    auto s = readable_asio_stream_for_file(ctx, f.path());

    unsigned message_count = 0;
    async_message_reader r(
        s,
        [&](gsl::span<std::uint8_t const> msg) -> bool {
            CHECK(msg.size() == 8);
            ++message_count;
            return true; // Notify quit.
        },
        [&](std::error_code ec) { CHECK_FALSE(ec); });
    r.start();
    ctx.run();
    CHECK(message_count == 1);
}

TEST_CASE("async_message_reader: message too long") {
    // Max message frame is 32k (including size prefix and padding).
    // When (size prefix) > (32768 - 4), the limit is exceeded.
    // 32765 = 0x7ffd.
    std::vector<std::uint8_t> v{0xfd, 0x7f, 0, 0}; // Little-endian

    testing::tempdir const td;
    auto f = testing::unique_file_with_data(
        td.path(), testing::make_test_filename(__FILE__, __LINE__), v);
    asio::io_context ctx;
    auto s = readable_asio_stream_for_file(ctx, f.path());

    bool ended = false;
    async_message_reader r(
        s,
        [&](gsl::span<std::uint8_t const> msg) -> bool {
            CHECK(false);
            (void)msg;
            return false;
        },
        [&](std::error_code ec) {
            CHECK(ec);
            CHECK(ec == std::error_code(errc::message_too_long));
            ended = true;
        });
    r.start();
    ctx.run();
    CHECK(ended);
}

TEST_CASE("async_message_reader: eof in message") {
    // Use size prefix 32764 (one less than that which triggers
    // message-too-long) so that we also confirm that the maximum size works.
    // 32764 = 0x7ffc.
    std::vector<std::uint8_t> v{0xfc, 0x7f, 0, 0}; // Little-endian

    testing::tempdir const td;
    auto f = testing::unique_file_with_data(
        td.path(), testing::make_test_filename(__FILE__, __LINE__), v);
    asio::io_context ctx;
    auto s = readable_asio_stream_for_file(ctx, f.path());

    bool ended = false;
    async_message_reader r(
        s,
        [&](gsl::span<std::uint8_t const> msg) -> bool {
            CHECK(false);
            (void)msg;
            return false;
        },
        [&](std::error_code ec) {
            CHECK(ec);
            CHECK(ec == std::error_code(errc::eof_in_message));
            ended = true;
        });
    r.start();
    ctx.run();
    CHECK(ended);
}

} // namespace partake::daemon
