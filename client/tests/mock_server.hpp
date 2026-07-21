/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asio.hpp"
#include "message.hpp"
#include "partake_protocol_generated.h"
#include "posix.hpp"
#include "shmem_mmap.hpp"
#include "testing.hpp"
#include "win32.hpp"

#include <gsl/span>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace partake::client {

// A scriptable in-process daemon stand-in, listening on a Unix domain
// socket and running its own I/O thread (the test thread typically blocks
// in queue::wait_one()). Speaks the hello exchange and answers Ping/Quit;
// deviations are scripted via 'options' and the post-based members.
// Handles a single connection (the first one accepted).
class mock_server {
  public:
    enum class hello_mode { accept, reject, close_without_reply };

    struct options {
        hello_mode hello = hello_mode::accept;
        std::uint32_t conn_no = 42;
        bool respond_to_ping = true;        // false: record and withhold.
        bool respond_to_quit = true;        // false: close without replying.
        std::size_t segment_size = 16384;   // One page (macOS/Linux default).
        bool respond_to_get_segment = true; // false: NO_SUCH_SEGMENT.
    };

#ifdef _WIN32
    using unlinkable_type = common::win32::unlinkable;
#else
    using unlinkable_type = common::posix::unlinkable;
#endif

  private:
    using socket_type = asio::local::stream_protocol::socket;

    options opts;
    daemon::mmap_shmem segment; // Real shm_open segment served for segment 0.
    testing::tempdir td;
    std::string path;
    unlinkable_type unlk; // Declared after td: unlinked before td removal.
    asio::io_context ctx;
    asio::executor_work_guard<asio::io_context::executor_type> guard;
    asio::local::stream_protocol::acceptor acceptor;
    socket_type sock;
    common::async_message_writer<socket_type, std::vector<std::uint8_t>>
        writer;
    common::async_message_reader<socket_type> reader;
    bool said_hello = false; // Server thread only.
    // Flush-then-close bookkeeping (server thread only): writes issued
    // minus completed; once close_after_flush is set, the socket closes
    // when this reaches zero.
    int outstanding_writes = 0;
    bool close_after_flush = false;
    std::atomic<int> n_pings{0};
    std::atomic<int> n_quits{0};
    std::atomic<int> n_get_segments{0};
    std::thread server_thread; // Last: started after everything else.

  public:
    mock_server() : mock_server(options{}) {}
    explicit mock_server(options options);
    ~mock_server(); // Closes the socket, stops and joins the I/O thread.

    mock_server(mock_server const &) = delete;
    auto operator=(mock_server const &) -> mock_server & = delete;
    mock_server(mock_server &&) = delete;
    auto operator=(mock_server &&) -> mock_server & = delete;

    [[nodiscard]] auto socket_path() const -> std::string { return path; }

    [[nodiscard]] auto pings_received() const -> int { return n_pings; }
    [[nodiscard]] auto quits_received() const -> int { return n_quits; }
    [[nodiscard]] auto get_segments_received() const -> int {
        return n_get_segments;
    }

    // The real segment served for segment 0.
    [[nodiscard]] auto segment_name() const -> std::string {
        return segment.name();
    }
    [[nodiscard]] auto segment_address() const noexcept -> void * {
        return segment.address();
    }
    [[nodiscard]] auto segment_bytes() const noexcept -> std::size_t {
        return segment.size();
    }

    // Run a task on the server's I/O thread.
    void post(std::function<void()> task);

    // The following are posted to the server's I/O thread. The raw frame
    // must satisfy the framing convention (8-byte-aligned length covered by
    // the size prefix).
    void send_raw_frame(std::vector<std::uint8_t> frame);
    void send_error_response(std::uint64_t seqno, protocol::Status status);
    void send_response_to_unknown_seqno();
    void close_connection();

  private:
    auto handle_message(gsl::span<std::uint8_t const> bytes) -> bool;
    auto handle_hello(gsl::span<std::uint8_t const> bytes) -> bool;
    auto handle_requests(gsl::span<std::uint8_t const> bytes) -> bool;
    void write_frame(flatbuffers::DetachedBuffer const &buf);
    void write_server_hello(protocol::HelloResult result,
                            std::uint32_t conn_no);
    void close_when_writes_flushed();
    void close_sock();
};

} // namespace partake::client
