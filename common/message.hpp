/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asio.hpp"
#include "errors.hpp"

#include <doctest.h>
#include <flatbuffers/flatbuffers.h>
#include <fmt/core.h>
#include <gsl/pointers>
#include <gsl/span>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace partake::common {

constexpr std::size_t message_frame_alignment = 8;
constexpr std::size_t max_message_frame_len = 32768;

namespace internal {

constexpr auto round_size_up_to_alignment(std::size_t s) noexcept
    -> std::size_t {
    return (s + message_frame_alignment - 1) & ~(message_frame_alignment - 1);
}

TEST_CASE("round_size_up_to_alignment") {
    CHECK(round_size_up_to_alignment(0) == 0);
    CHECK(round_size_up_to_alignment(1) == 8);
    CHECK(round_size_up_to_alignment(7) == 8);
    CHECK(round_size_up_to_alignment(8) == 8);
    CHECK(round_size_up_to_alignment(9) == 16);
    CHECK(round_size_up_to_alignment(4097) == 4104);
}

[[nodiscard]] inline auto
read_message_frame_size(gsl::span<std::uint8_t const> bytes) noexcept
    -> std::size_t {
    if (bytes.size() < sizeof(flatbuffers::uoffset_t))
        return 0;
    auto const fblen = flatbuffers::GetPrefixedSize(bytes.data());
    auto const msglen = fblen + sizeof(flatbuffers::uoffset_t);
    auto const framelen = round_size_up_to_alignment(msglen);
    return framelen;
}

TEST_CASE("read_message_frame_size") {
    std::array<std::uint8_t, 5> bytes{};
    auto s = gsl::make_span(bytes);
    CHECK(read_message_frame_size(s) == 8); // Prefix size + alignment padding
    CHECK(read_message_frame_size(s.subspan(0, 4)) == 8);
    CHECK(read_message_frame_size(s.subspan(0, 3)) == 0);
    CHECK(read_message_frame_size(s.subspan(0, 0)) == 0);
    bytes[1] = 1; // Prefix set to 256 (FlatBuffers is little endian)
    CHECK(read_message_frame_size(s) == 264); // Add prefix and padding
}

} // namespace internal

// Buffer can be flatbuffers::DetachedBuffer or anything with size() and data()
// that owns its storage and can be move-constructed without invalidating
// data().
// CompletionFunc takes error_code (std or boost) and returns void.
template <typename Socket, typename Buffer> class async_message_writer {
  public:
    using socket_type = Socket;
    using buffer_type = Buffer;

  private:
    gsl::not_null<socket_type *> sock;
    std::function<void(std::error_code)> handle_cmpl;
    std::vector<buffer_type> buffers_being_written;
    std::size_t next_index_to_write = 0; // 0 indicates no write in flight.
    std::vector<buffer_type> buffers_to_write_next;

  public:
    explicit async_message_writer(
        socket_type &socket,
        std::function<void(std::error_code)> handle_completion)
        : sock(&socket), handle_cmpl(std::move(handle_completion)) {}

    ~async_message_writer() = default;
    async_message_writer(async_message_writer const &) = delete;
    auto operator=(async_message_writer const &) = delete;
    async_message_writer(async_message_writer &&) = delete;
    auto operator=(async_message_writer &&) = delete;

    void async_write_message(buffer_type &&buffer) {
        if (buffer.size() == 0)
            return asio::defer(sock->get_executor(),
                               [this] { handle_cmpl({}); });

        buffers_to_write_next.emplace_back(std::move(buffer));
        if (not is_write_in_progress())
            start_writing_buffers();
    }

  private:
    [[nodiscard]] auto is_write_in_progress() const noexcept -> bool {
        return next_index_to_write != 0;
    }

    void start_writing_buffers() {
        assert(not is_write_in_progress());
        using std::swap;
        swap(buffers_being_written, buffers_to_write_next);
        schedule_next_write();
    }

    void schedule_next_write() {
        buffer_type buf =
            std::move(buffers_being_written[next_index_to_write]);
        ++next_index_to_write;

        // The FlatBuffers docs do not specify how the _end_ of a constructed
        // buffer is aligned. Because we send buffers one after another, it is
        // important that each buffer have a size that is a multiple of the
        // required alignment (8). So we add the necessary zero bytes if
        // necessary. We do not adjust the size prefix, as we round up the size
        // on the receiving end.
        auto aligned_size = internal::round_size_up_to_alignment(buf.size());
        auto pad_size = aligned_size - buf.size();

        static constexpr std::array<std::uint8_t, message_frame_alignment>
            zeros{};
        auto pad_bytes = gsl::span(zeros).first(pad_size);
        assert(pad_bytes.size() == pad_size);

        auto buffers = std::array{
            asio::const_buffer(buf.data(), buf.size()),
            asio::const_buffer(pad_bytes.data(), pad_bytes.size()),
        };

        // 'buffers' points to data in 'buf' and 'zeros'. We keep 'buf'
        // available until the write finishes by moving it into the lambda
        // below.

        asio::async_write(
            *sock, buffers,
            [this, b = std::move(buf)](boost::system::error_code err,
                                       std::size_t written) {
                handle_cmpl(err);
                if (err) {
                    buffers_being_written.clear();
                    next_index_to_write = 0;
                    buffers_to_write_next.clear();
                    return;
                }

                assert(written ==
                       internal::round_size_up_to_alignment(b.size()));

                if (buffers_being_written.size() > next_index_to_write) {
                    schedule_next_write();
                } else { // Finished current buffers_being_written.
                    buffers_being_written.clear();
                    next_index_to_write = 0;
                    if (not buffers_to_write_next.empty()) {
                        start_writing_buffers();
                    }
                }
            });
    }
};

// Continuously (and asynchronously) read from socket and delimit messages.
// Stop only when there is a read error (including because the socket was shut
// down) or the message handler indicated end of processing.
// Any message (appearing to be) larger than the allowed maximum is treated as
// a read error.
template <typename Socket> class async_message_reader {
  public:
    using socket_type = Socket;

  private:
    gsl::not_null<socket_type *> sock;
    std::function<auto(gsl::span<std::uint8_t const>)->bool> handle_msg;
    std::function<void(std::error_code)> handle_ed;

    // Read buffer only grows, up to a maximum of max_message_frame_len.
    std::vector<std::uint8_t> readbuf;
    static constexpr std::size_t initial_readbuf_size = 1024;

  public:
    explicit async_message_reader(
        socket_type &socket,
        std::function<auto(gsl::span<std::uint8_t const>)->bool>
            handle_message,
        std::function<void(std::error_code)> handle_end)
        : sock(&socket), handle_msg(std::move(handle_message)),
          handle_ed(std::move(handle_end)), readbuf(initial_readbuf_size) {}

    ~async_message_reader() = default;
    async_message_reader(async_message_reader const &) = delete;
    auto operator=(async_message_reader const &) = delete;
    async_message_reader(async_message_reader &&) = delete;
    auto operator=(async_message_reader &&) = delete;

    void start() { schedule_read(); }

  private:
    void schedule_read(std::size_t start = 0) {
        auto new_read = gsl::span(readbuf).subspan(start);
        sock->async_read_some(
            asio::buffer(new_read.data(), new_read.size()),
            [this, start](boost::system::error_code err,
                          std::size_t bytes_read) {
                if (err && err.value() != asio::error::eof)
                    return handle_ed(err);

                auto remaining = gsl::span(std::as_const(readbuf))
                                     .first(start + bytes_read);
                std::size_t frame_size = 0;
                for (;;) {
                    frame_size = internal::read_message_frame_size(remaining);
                    if (frame_size == 0 || frame_size > remaining.size())
                        break; // Complete frame not yet available
                    bool done = handle_msg(remaining.first(frame_size));
                    if (done)
                        return handle_ed({});
                    remaining = remaining.subspan(frame_size);
                }
                auto const remaining_size = remaining.size();
                std::copy(remaining.begin(), remaining.end(), readbuf.begin());

                // Ensure the rest of any partial frame will fit in the buffer
                // on next read.
                if (frame_size > max_message_frame_len)
                    return handle_ed(std::error_code(errc::message_too_long));

                if (frame_size > readbuf.size()) {
                    // Grow to fit the next message frame, but to at least 1.5x
                    // of current size to keep resizing infrequent.
                    readbuf.resize(std::max(frame_size,
                                            std::min(max_message_frame_len,
                                                     3 * readbuf.size() / 2)));
                }

                if (err) { // EOF was reached.
                    if (remaining_size > 0) {
                        return handle_ed(
                            std::error_code(errc::eof_in_message));
                    }
                    return handle_ed({});
                }

                schedule_read(remaining_size);
            });
    }
};

} // namespace partake::common
