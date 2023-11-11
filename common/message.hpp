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
    // NOLINTBEGIN(readability-magic-numbers)
    std::array<std::uint8_t, 5> bytes{};
    auto s = gsl::make_span(bytes);
    CHECK(read_message_frame_size(s) == 8); // Prefix size + alignment padding
    CHECK(read_message_frame_size(s.subspan(0, 4)) == 8);
    CHECK(read_message_frame_size(s.subspan(0, 3)) == 0);
    CHECK(read_message_frame_size(s.subspan(0, 0)) == 0);
    bytes[1] = 1; // Prefix set to 256 (FlatBuffers is little endian)
    CHECK(read_message_frame_size(s) == 264); // Add prefix and padding
    // NOLINTEND(readability-magic-numbers)
}

} // namespace internal

// Buffer can be flatbuffers::DetachedBuffer or anything with size() and data()
// that owns its storage and is movable.
template <typename Socket, typename Buffer> class async_message_writer {
  public:
    using socket_type = Socket;
    using buffer_type = Buffer;

  private:
    gsl::not_null<socket_type *> sock;
    std::function<void(std::error_code)> handle_cmpl;
    std::vector<buffer_type> buffers_to_write_next;
    std::vector<buffer_type> buffers_being_written;
    std::vector<asio::const_buffer> asio_buffers;
    std::vector<std::size_t> end_offsets;

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

        buffers_to_write_next.push_back(std::move(buffer));
        if (not is_write_in_progress())
            start_writing();
    }

  private:
    [[nodiscard]] auto is_write_in_progress() const noexcept -> bool {
        return not buffers_being_written.empty();
    }

    void start_writing() {
        assert(not is_write_in_progress());
        assert(asio_buffers.empty());
        assert(end_offsets.empty());

        using std::swap;
        swap(buffers_being_written, buffers_to_write_next);
        assert(is_write_in_progress());

        // Fill asio_buffers with references to memory owned by
        // buffers_being_written (and the static 'zeros').
        std::size_t end_offset = 0;
        for (buffer_type &buf : buffers_being_written) {
            asio_buffers.emplace_back(buf.data(), buf.size());

            // The FlatBuffers docs do not specify how the _end_ of a
            // constructed buffer is aligned. Because we send buffers one after
            // another, it is important that each buffer have a size that is a
            // multiple of the required alignment (8). So we add the necessary
            // zero bytes if necessary. We do not adjust the size prefix, as we
            // round up the size on the receiving end.
            static constexpr std::array<std::uint8_t, message_frame_alignment>
                zeros{};
            auto aligned_size =
                internal::round_size_up_to_alignment(buf.size());
            auto pad_size = aligned_size - buf.size();
            if (pad_size > 0)
                asio_buffers.emplace_back(zeros.data(), pad_size);

            end_offset += aligned_size;
            end_offsets.push_back(end_offset);
        }

        asio::async_write(
            *sock, asio_buffers,
            [this](boost::system::error_code err, std::size_t written) {
                static const boost::system::error_code canceled =
                    asio::error::operation_aborted;

                asio_buffers.clear();
                buffers_being_written.clear();

                bool had_error = false;
                for (auto off : end_offsets) {
                    if (written >= off) {
                        handle_cmpl({});
                    } else {
                        if (not had_error) {
                            assert(err);
                            handle_cmpl(err);
                            had_error = true;
                        } else {
                            handle_cmpl(canceled);
                        }
                    }
                }
                end_offsets.clear();

                if (err) {
                    for ([[maybe_unused]] auto &b : buffers_to_write_next)
                        handle_cmpl(canceled);
                    buffers_to_write_next.clear();
                } else if (not buffers_to_write_next.empty()) {
                    start_writing();
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
                    bool const done = handle_msg(remaining.first(frame_size));
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
