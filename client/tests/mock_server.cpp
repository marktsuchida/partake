/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "mock_server.hpp"

#include "framing.hpp"
#include "request_handler.hpp" // daemon::internal::segment_spec_to_fb
#include "requests.hpp"
#include "response_builder.hpp"
#include "segment.hpp"

#include <catch2/catch_test_macros.hpp>

#include <flatbuffers/flatbuffers.h>

#include <array>
#include <cassert>
#include <utility>

namespace partake::client {

mock_server::mock_server(options options)
    : opts(options),
      segment(daemon::create_posix_mmap_shmem(opts.segment_size)),
      path(testing::unique_path(td.path(), "mock-server").string()),
      unlk(path), guard(asio::make_work_guard(ctx)), acceptor(ctx), sock(ctx),
      writer(sock,
             [this](std::error_code) {
                 --outstanding_writes;
                 if (close_after_flush and outstanding_writes == 0)
                     close_sock();
             }),
      reader(
          sock,
          [this](gsl::span<std::uint8_t const> bytes) {
              return handle_message(bytes);
          },
          [](std::error_code) {}) {
    asio::local::stream_protocol::endpoint const endpt(path);
    acceptor.open(endpt.protocol());
    acceptor.bind(endpt);
    acceptor.listen(socket_type::max_listen_connections);
    acceptor.async_accept(sock, [this](boost::system::error_code ec) {
        if (not ec)
            reader.start({}); // Members outlive the joined thread.
    });
    server_thread = std::thread([this] { ctx.run(); });
}

mock_server::~mock_server() {
    asio::post(ctx, [this] {
        boost::system::error_code ignore;
        (void)acceptor.close(ignore);
        close_sock();
    });
    guard.reset();
    server_thread.join();
}

void mock_server::post(std::function<void()> task) {
    asio::post(ctx, std::move(task));
}

void mock_server::send_raw_frame(std::vector<std::uint8_t> frame) {
    post([this, frame = std::move(frame)]() mutable {
        ++outstanding_writes;
        writer.async_write_message(std::move(frame), {});
    });
}

void mock_server::send_error_response(std::uint64_t seqno,
                                      protocol::Status status) {
    post([this, seqno, status] {
        auto rb = daemon::response_builder(
            [this](flatbuffers::DetachedBuffer const &buf) {
                write_frame(buf);
            },
            1);
        rb.add_error_response(seqno, status);
        rb.flush();
    });
}

void mock_server::send_response_to_unknown_seqno() {
    post([this] {
        auto rb = daemon::response_builder(
            [this](flatbuffers::DetachedBuffer const &buf) {
                write_frame(buf);
            },
            1);
        static constexpr std::uint64_t bogus_seqno = 1000000;
        rb.add_successful_response(
            bogus_seqno, protocol::CreatePingResponse(rb.fbbuilder()));
        rb.flush();
    });
}

void mock_server::close_connection() {
    post([this] { close_sock(); });
}

void mock_server::release_withheld_alloc_responses() {
    post([this] {
        for (auto const &w : withheld_allocs) {
            auto rb = daemon::response_builder(
                [this](flatbuffers::DetachedBuffer const &buf) {
                    write_frame(buf);
                },
                1);
            protocol::Mapping const mapping(w.key, 0, w.offset, w.size);
            rb.add_successful_response(
                w.seqno, protocol::CreateAllocResponse(
                             rb.fbbuilder(), &mapping, opts.alloc_zeroed));
            rb.flush();
        }
        withheld_allocs.clear();
    });
}

void mock_server::release_withheld_open_responses() {
    post([this] {
        for (auto const &w : withheld_opens) {
            auto const it = objects.find(w.key);
            assert(it != objects.end()); // Recorded only for known keys.
            auto rb = daemon::response_builder(
                [this](flatbuffers::DetachedBuffer const &buf) {
                    write_frame(buf);
                },
                1);
            protocol::Mapping const mapping(w.key, 0, it->second.first,
                                            it->second.second);
            rb.add_successful_response(w.seqno, protocol::CreateOpenResponse(
                                                    rb.fbbuilder(), &mapping));
            rb.flush();
        }
        withheld_opens.clear();
    });
}

void mock_server::release_withheld_share_responses() {
    post([this] {
        for (auto const &w : withheld_shares) {
            auto rb = daemon::response_builder(
                [this](flatbuffers::DetachedBuffer const &buf) {
                    write_frame(buf);
                },
                1);
            rb.add_successful_response(
                w.seqno, protocol::CreateShareResponse(rb.fbbuilder()));
            rb.flush();
        }
        withheld_shares.clear();
    });
}

void mock_server::release_withheld_unshare_responses() {
    post([this] {
        for (auto const &w : withheld_unshares) {
            auto rb = daemon::response_builder(
                [this](flatbuffers::DetachedBuffer const &buf) {
                    write_frame(buf);
                },
                1);
            rb.add_successful_response(
                w.seqno, protocol::CreateUnshareResponse(rb.fbbuilder(),
                                                         w.new_key, false));
            rb.flush();
        }
        withheld_unshares.clear();
    });
}

void mock_server::release_withheld_create_voucher_responses() {
    post([this] {
        for (auto const &w : withheld_create_vouchers) {
            auto rb = daemon::response_builder(
                [this](flatbuffers::DetachedBuffer const &buf) {
                    write_frame(buf);
                },
                1);
            rb.add_successful_response(
                w.seqno, protocol::CreateCreateVoucherResponse(rb.fbbuilder(),
                                                               w.voucher_key));
            rb.flush();
        }
        withheld_create_vouchers.clear();
    });
}

auto mock_server::handle_message(gsl::span<std::uint8_t const> bytes) -> bool {
    if (not said_hello)
        return handle_hello(bytes);
    return handle_requests(bytes);
}

auto mock_server::handle_hello(gsl::span<std::uint8_t const> bytes) -> bool {
    auto verifier = flatbuffers::Verifier(bytes.data(), bytes.size());
    if (not verifier.VerifySizePrefixedBuffer<protocol::ClientHelloMessage>(
            nullptr)) {
        close_sock();
        return true;
    }
    switch (opts.hello) {
    case hello_mode::accept:
        said_hello = true;
        write_server_hello(protocol::HelloResult::OK, opts.conn_no);
        return false;
    case hello_mode::reject:
        write_server_hello(protocol::HelloResult::UNSUPPORTED_PROTOCOL_VERSION,
                           0);
        close_when_writes_flushed();
        return true;
    case hello_mode::close_without_reply:
        close_sock();
        return true;
    }
    assert(false);
    return true;
}

auto mock_server::handle_requests(gsl::span<std::uint8_t const> bytes)
    -> bool {
    auto verifier = flatbuffers::Verifier(bytes.data(), bytes.size());
    if (not verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(
            nullptr)) {
        close_sock();
        return true;
    }
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(
            bytes.data());
    auto const *requests = msg->requests();
    auto rb = daemon::response_builder(
        [this](flatbuffers::DetachedBuffer const &buf) { write_frame(buf); },
        requests != nullptr ? requests->size() : 0);
    bool done = false;
    if (requests != nullptr) {
        for (auto const *req : *requests) {
            switch (req->request_type()) {
            case protocol::AnyRequest::PingRequest:
                ++n_pings;
                if (opts.respond_to_ping) {
                    rb.add_successful_response(
                        req->seqno(),
                        protocol::CreatePingResponse(rb.fbbuilder()));
                }
                break;
            case protocol::AnyRequest::QuitRequest:
                ++n_quits;
                if (opts.respond_to_quit) {
                    rb.add_successful_response(
                        req->seqno(),
                        protocol::CreateQuitResponse(rb.fbbuilder()));
                }
                done = true;
                break;
            case protocol::AnyRequest::AllocRequest: {
                ++n_allocs;
                auto const *areq = req->request_as_AllocRequest();
                auto const key = next_key++;
                auto const offset = bump_offset;
                bump_offset += (areq->size() + 7) / 8 * 8; // 8-byte-aligned.
                objects[key] = {offset, areq->size()};
                if (opts.respond_to_alloc) {
                    protocol::Mapping const mapping(key, 0, offset,
                                                    areq->size());
                    rb.add_successful_response(
                        req->seqno(),
                        protocol::CreateAllocResponse(rb.fbbuilder(), &mapping,
                                                      opts.alloc_zeroed));
                } else {
                    withheld_allocs.push_back(
                        {req->seqno(), key, offset, areq->size()});
                }
                break;
            }
            case protocol::AnyRequest::OpenRequest: {
                ++n_opens;
                auto const *oreq = req->request_as_OpenRequest();
                auto key = oreq->key();
                // A voucher key resolves to its target, consuming one
                // redemption; Mapping.key is always the object key.
                auto const vit = vouchers.find(key);
                if (vit != vouchers.end()) {
                    key = vit->second.first;
                    if (--vit->second.second == 0)
                        vouchers.erase(vit);
                }
                auto const it = objects.find(key);
                if (it == objects.end()) {
                    rb.add_error_response(req->seqno(),
                                          protocol::Status::NO_SUCH_OBJECT);
                } else if (opts.respond_to_open) {
                    protocol::Mapping const mapping(key, 0, it->second.first,
                                                    it->second.second);
                    rb.add_successful_response(req->seqno(),
                                               protocol::CreateOpenResponse(
                                                   rb.fbbuilder(), &mapping));
                } else {
                    withheld_opens.push_back({req->seqno(), key});
                }
                break;
            }
            case protocol::AnyRequest::ShareRequest: {
                ++n_shares;
                auto const key = req->request_as_ShareRequest()->key();
                if (objects.find(key) == objects.end()) {
                    rb.add_error_response(req->seqno(),
                                          protocol::Status::NO_SUCH_OBJECT);
                } else if (opts.respond_to_share) {
                    rb.add_successful_response(
                        req->seqno(),
                        protocol::CreateShareResponse(rb.fbbuilder()));
                } else {
                    withheld_shares.push_back({req->seqno(), key});
                }
                break;
            }
            case protocol::AnyRequest::UnshareRequest: {
                ++n_unshares;
                auto const key = req->request_as_UnshareRequest()->key();
                auto const it = objects.find(key);
                if (it == objects.end()) {
                    rb.add_error_response(req->seqno(),
                                          protocol::Status::NO_SUCH_OBJECT);
                } else {
                    // Rekey in place, as the daemon would.
                    auto const new_key = next_key++;
                    objects[new_key] = it->second;
                    objects.erase(it);
                    if (opts.respond_to_unshare) {
                        rb.add_successful_response(
                            req->seqno(), protocol::CreateUnshareResponse(
                                              rb.fbbuilder(), new_key, false));
                    } else {
                        withheld_unshares.push_back({req->seqno(), new_key});
                    }
                }
                break;
            }
            case protocol::AnyRequest::CloseRequest:
                ++n_closes;
                objects.erase(req->request_as_CloseRequest()->key());
                rb.add_successful_response(
                    req->seqno(),
                    protocol::CreateCloseResponse(rb.fbbuilder()));
                break;
            case protocol::AnyRequest::CreateVoucherRequest: {
                ++n_create_vouchers;
                auto const *vreq = req->request_as_CreateVoucherRequest();
                auto target = vreq->key();
                auto const vit = vouchers.find(target);
                if (vit != vouchers.end())
                    target = vit->second.first;
                if (objects.find(target) == objects.end()) {
                    rb.add_error_response(req->seqno(),
                                          protocol::Status::NO_SUCH_OBJECT);
                } else if (vreq->count() == 0) {
                    rb.add_error_response(req->seqno(),
                                          protocol::Status::INVALID_REQUEST);
                } else {
                    auto const vkey = next_key++;
                    vouchers[vkey] = {target, vreq->count()};
                    if (opts.respond_to_create_voucher) {
                        rb.add_successful_response(
                            req->seqno(),
                            protocol::CreateCreateVoucherResponse(
                                rb.fbbuilder(), vkey));
                    } else {
                        withheld_create_vouchers.push_back(
                            {req->seqno(), vkey});
                    }
                }
                break;
            }
            case protocol::AnyRequest::DiscardVoucherRequest: {
                ++n_discard_vouchers;
                auto const key =
                    req->request_as_DiscardVoucherRequest()->key();
                auto const vit = vouchers.find(key);
                if (vit != vouchers.end()) {
                    auto const target = vit->second.first;
                    vouchers.erase(vit);
                    rb.add_successful_response(
                        req->seqno(), protocol::CreateDiscardVoucherResponse(
                                          rb.fbbuilder(), target));
                } else if (objects.find(key) != objects.end()) {
                    // Documented no-op on an ordinary object key.
                    rb.add_successful_response(
                        req->seqno(), protocol::CreateDiscardVoucherResponse(
                                          rb.fbbuilder(), key));
                } else {
                    rb.add_error_response(req->seqno(),
                                          protocol::Status::NO_SUCH_OBJECT);
                }
                break;
            }
            case protocol::AnyRequest::GetSegmentRequest:
                ++n_get_segments;
                if (opts.respond_to_get_segment) {
                    daemon::segment_spec const dspec{
                        daemon::posix_mmap_segment_spec{segment.name()},
                        segment.size()};
                    auto seg = daemon::internal::segment_spec_to_fb(
                        rb.fbbuilder(), dspec);
                    rb.add_successful_response(
                        req->seqno(), protocol::CreateGetSegmentResponse(
                                          rb.fbbuilder(), seg));
                } else {
                    rb.add_error_response(req->seqno(),
                                          protocol::Status::NO_SUCH_SEGMENT);
                }
                break;
            default:
                break; // Not needed by these tests.
            }
            if (done)
                break;
        }
    }
    rb.flush();
    if (done) // Close (after flushing any reply), as the daemon would.
        close_when_writes_flushed();
    return done;
}

void mock_server::write_frame(flatbuffers::DetachedBuffer const &buf) {
    ++outstanding_writes;
    writer.async_write_message(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        std::vector<std::uint8_t>(buf.data(), buf.data() + buf.size()), {});
}

void mock_server::write_server_hello(protocol::HelloResult result,
                                     std::uint32_t conn_no) {
    auto fbb = flatbuffers::FlatBufferBuilder();
    fbb.FinishSizePrefixed(protocol::CreateServerHelloMessage(
        fbb, result,
        static_cast<std::uint32_t>(protocol::ProtocolVersion::CURRENT),
        conn_no));
    write_frame(fbb.Release());
}

void mock_server::close_when_writes_flushed() {
    close_after_flush = true;
    if (outstanding_writes == 0)
        close_sock();
}

void mock_server::close_sock() {
    boost::system::error_code ignore;
    (void)sock.shutdown(socket_type::shutdown_both, ignore);
    (void)sock.close(ignore);
}

// Fixture self-tests, using a raw synchronous socket as the client so that
// the fixture is validated independently of the partake client library.

namespace {

// Pad a size-prefixed FlatBuffer to the framing convention, the way
// common::async_message_writer would.
auto pad_frame(flatbuffers::DetachedBuffer const &buf)
    -> std::vector<std::uint8_t> {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::vector<std::uint8_t> frame(buf.data(), buf.data() + buf.size());
    auto const padded =
        common::round_size_up_to_message_frame_alignment(frame.size());
    if (padded != frame.size()) {
        frame.resize(padded);
        flatbuffers::WriteScalar(frame.data(),
                                 static_cast<flatbuffers::uoffset_t>(
                                     padded - sizeof(flatbuffers::uoffset_t)));
    }
    return frame;
}

// Read one whole frame (as written by common::async_message_writer, whose
// size prefix covers any padding).
auto read_frame(asio::local::stream_protocol::socket &s)
    -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> frame(sizeof(flatbuffers::uoffset_t));
    asio::read(s, asio::buffer(frame));
    auto const len = flatbuffers::GetPrefixedSize(frame.data());
    frame.resize(sizeof(flatbuffers::uoffset_t) + len);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    asio::read(
        s, asio::buffer(frame.data() + sizeof(flatbuffers::uoffset_t), len));
    return frame;
}

auto connect_and_say_hello(asio::io_context &ctx, mock_server const &srv)
    -> asio::local::stream_protocol::socket {
    asio::local::stream_protocol::socket s(ctx);
    s.connect(asio::local::stream_protocol::endpoint(srv.socket_path()));
    asio::write(
        s, asio::buffer(pad_frame(internal::make_client_hello("self-test"))));
    return s;
}

} // namespace

TEST_CASE("mock_server: constructs and destructs without hanging") {
    mock_server const srv;
    CHECK_FALSE(srv.socket_path().empty());
}

TEST_CASE("mock_server: hello accept round trip") {
    mock_server const srv;
    asio::io_context ctx; // Synchronous ops only; never run.
    auto s = connect_and_say_hello(ctx, srv);

    auto const reply = read_frame(s);
    REQUIRE(internal::verify_server_hello_message(reply));
    auto const *hello =
        flatbuffers::GetSizePrefixedRoot<protocol::ServerHelloMessage>(
            reply.data());
    CHECK(hello->result() == protocol::HelloResult::OK);
    CHECK(hello->conn_no() == 42); // NOLINT(readability-magic-numbers)
}

TEST_CASE("mock_server: hello reject round trip, then close") {
    mock_server const srv({.hello = mock_server::hello_mode::reject});
    asio::io_context ctx;
    auto s = connect_and_say_hello(ctx, srv);

    auto const reply = read_frame(s);
    REQUIRE(internal::verify_server_hello_message(reply));
    auto const *hello =
        flatbuffers::GetSizePrefixedRoot<protocol::ServerHelloMessage>(
            reply.data());
    CHECK(hello->result() ==
          protocol::HelloResult::UNSUPPORTED_PROTOCOL_VERSION);

    boost::system::error_code ec;
    std::array<std::uint8_t, 1> byte{};
    (void)asio::read(s, asio::buffer(byte), ec);
    CHECK(ec == asio::error::eof);
}

TEST_CASE("mock_server: hello close-without-reply") {
    mock_server const srv(
        {.hello = mock_server::hello_mode::close_without_reply});
    asio::io_context ctx;
    auto s = connect_and_say_hello(ctx, srv);

    boost::system::error_code ec;
    std::array<std::uint8_t, 1> byte{};
    (void)asio::read(s, asio::buffer(byte), ec);
    CHECK(ec == asio::error::eof);
}

namespace {

auto send_get_segment(asio::local::stream_protocol::socket &s,
                      std::uint64_t seqno, std::uint32_t segment_no) {
    auto rb = internal::request_builder(1);
    internal::add_get_segment_request(rb, seqno, segment_no);
    asio::write(s, asio::buffer(pad_frame(rb.release_buffer())));
}

} // namespace

TEST_CASE("mock_server: get_segment returns the real segment") {
    mock_server const srv;
    asio::io_context ctx;
    auto s = connect_and_say_hello(ctx, srv);
    (void)read_frame(s); // ServerHello

    send_get_segment(s, 99, 0);
    auto const reply = read_frame(s);
    REQUIRE(internal::verify_response_message(reply));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
            reply.data());
    auto const *resp = msg->responses()->Get(0);
    CHECK(resp->seqno() == 99); // NOLINT(readability-magic-numbers)
    CHECK(resp->status() == protocol::Status::OK);
    REQUIRE(resp->response_type() ==
            protocol::AnyResponse::GetSegmentResponse);
    auto const *seg = resp->response_as_GetSegmentResponse()->segment();
    REQUIRE(seg != nullptr);
    CHECK(seg->size() == srv.segment_bytes());
    REQUIRE(seg->spec_type() == protocol::SegmentMappingSpec::PosixMmapSpec);
    auto const *spec = seg->spec_as_PosixMmapSpec();
    REQUIRE(spec->name() != nullptr);
    CHECK(spec->name()->str() == srv.segment_name());
    CHECK(spec->use_shm_open());
    CHECK(srv.get_segments_received() == 1);
}

TEST_CASE("mock_server: get_segment can report NO_SUCH_SEGMENT") {
    mock_server const srv({.respond_to_get_segment = false});
    asio::io_context ctx;
    auto s = connect_and_say_hello(ctx, srv);
    (void)read_frame(s); // ServerHello

    send_get_segment(s, 5, 0);
    auto const reply = read_frame(s);
    REQUIRE(internal::verify_response_message(reply));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
            reply.data());
    auto const *resp = msg->responses()->Get(0);
    CHECK(resp->seqno() == 5);
    CHECK(resp->status() == protocol::Status::NO_SUCH_SEGMENT);
    CHECK(resp->response_type() == protocol::AnyResponse::NONE);
    CHECK(srv.get_segments_received() == 1);
}

namespace {

auto send_alloc(asio::local::stream_protocol::socket &s, std::uint64_t seqno,
                std::uint64_t size) {
    auto rb = internal::request_builder(1);
    internal::add_alloc_request(rb, seqno, size, protocol::Policy::DEFAULT);
    asio::write(s, asio::buffer(pad_frame(rb.release_buffer())));
}

auto send_open(asio::local::stream_protocol::socket &s, std::uint64_t seqno,
               std::uint64_t key) {
    auto rb = internal::request_builder(1);
    internal::add_open_request(rb, seqno, key, protocol::Policy::DEFAULT,
                               true);
    asio::write(s, asio::buffer(pad_frame(rb.release_buffer())));
}

auto send_close(asio::local::stream_protocol::socket &s, std::uint64_t seqno,
                std::uint64_t key) {
    auto rb = internal::request_builder(1);
    internal::add_close_request(rb, seqno, key);
    asio::write(s, asio::buffer(pad_frame(rb.release_buffer())));
}

auto single_response_of(std::vector<std::uint8_t> const &reply)
    -> protocol::Response const * {
    REQUIRE(internal::verify_response_message(reply));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
            reply.data());
    return msg->responses()->Get(0);
}

} // namespace

TEST_CASE("mock_server: alloc, open, close round trips") {
    mock_server const srv;
    asio::io_context ctx;
    auto s = connect_and_say_hello(ctx, srv);
    (void)read_frame(s); // ServerHello

    send_alloc(s, 10, 100);
    auto reply = read_frame(s);
    auto const *aresp = single_response_of(reply);
    CHECK(aresp->seqno() == 10);
    CHECK(aresp->status() == protocol::Status::OK);
    REQUIRE(aresp->response_type() == protocol::AnyResponse::AllocResponse);
    auto const *amap = aresp->response_as_AllocResponse()->object();
    REQUIRE(amap != nullptr);
    auto const key = amap->key();
    auto const alloc_offset = amap->offset(); // amap dies with 'reply'.
    CHECK(key != 0);
    CHECK(amap->segment() == 0);
    CHECK(amap->offset() + amap->size() <= srv.segment_bytes());
    CHECK(amap->size() == 100);
    CHECK(srv.allocs_received() == 1);

    send_open(s, 11, key);
    reply = read_frame(s);
    auto const *oresp = single_response_of(reply);
    CHECK(oresp->seqno() == 11);
    CHECK(oresp->status() == protocol::Status::OK);
    REQUIRE(oresp->response_type() == protocol::AnyResponse::OpenResponse);
    auto const *omap = oresp->response_as_OpenResponse()->object();
    REQUIRE(omap != nullptr);
    CHECK(omap->key() == key);
    CHECK(omap->offset() == alloc_offset);
    CHECK(omap->size() == 100);
    CHECK(srv.opens_received() == 1);

    send_open(s, 12, key + 1); // Never-allocated key.
    reply = read_frame(s);
    auto const *eresp = single_response_of(reply);
    CHECK(eresp->seqno() == 12);
    CHECK(eresp->status() == protocol::Status::NO_SUCH_OBJECT);

    send_close(s, 13, key);
    reply = read_frame(s);
    auto const *cresp = single_response_of(reply);
    CHECK(cresp->seqno() == 13);
    CHECK(cresp->status() == protocol::Status::OK);
    CHECK(cresp->response_type() == protocol::AnyResponse::CloseResponse);
    CHECK(srv.closes_received() == 1);

    send_open(s, 14, key); // Closed keys are forgotten.
    reply = read_frame(s);
    CHECK(single_response_of(reply)->status() ==
          protocol::Status::NO_SUCH_OBJECT);
}

} // namespace partake::client
