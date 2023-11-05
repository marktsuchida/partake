/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "request_handler.hpp"

#include <doctest.h>
#include <trompeloeil.hpp>

#include <cstdint>
#include <functional>

namespace partake::daemon {

namespace {

struct mock_resource {
    std::uint32_t seg;
    std::size_t off;
    std::size_t siz;
    [[nodiscard]] auto segment_id() const -> std::uint32_t { return seg; }
    [[nodiscard]] auto offset() const -> std::size_t { return off; }
    [[nodiscard]] auto size() const -> std::size_t { return siz; }
};

struct mock_session {
    struct object_type { // Only used by request_handler to get resource type.
        using resource_type = mock_resource;
    };

    MAKE_MOCK4(hello, void(std::string_view, std::uint32_t,
                           std::function<void(std::uint32_t)>,
                           std::function<void(protocol::Status)>));
    MAKE_MOCK3(get_segment,
               void(std::uint32_t, std::function<void(segment_spec)>,
                    std::function<void(protocol::Status)>));
    MAKE_MOCK4(alloc,
               void(std::uint64_t, protocol::Policy,
                    std::function<void(common::token, mock_resource const &)>,
                    std::function<void(protocol::Status)>));
    MAKE_MOCK8(open,
               void(common::token, protocol::Policy, bool, time_point,
                    std::function<void(common::token, mock_resource const &)>,
                    std::function<void(protocol::Status)>,
                    std::function<void(common::token, mock_resource const &)>,
                    std::function<void(protocol::Status)>));
    MAKE_MOCK3(close, void(common::token, std::function<void()>,
                           std::function<void(protocol::Status)>));
    MAKE_MOCK3(share, void(common::token, std::function<void()>,
                           std::function<void(protocol::Status)>));
    MAKE_MOCK6(unshare,
               void(common::token, bool, std::function<void(common::token)>,
                    std::function<void(protocol::Status)>,
                    std::function<void(common::token)>,
                    std::function<void(protocol::Status)>));
    MAKE_MOCK5(create_voucher, void(common::token, unsigned, time_point,
                                    std::function<void(common::token)>,
                                    std::function<void(protocol::Status)>));
    MAKE_MOCK4(discard_voucher, void(common::token, time_point,
                                     std::function<void(common::token)>,
                                     std::function<void(protocol::Status)>));

    MAKE_MOCK0(perform_housekeeping, void());
};

struct mock_writer {
    void operator()(flatbuffers::DetachedBuffer &&b) { call(std::move(b)); }
    MAKE_MOCK1(call, void(flatbuffers::DetachedBuffer &&));
};

struct mock_error_handler {
    void operator()(std::error_code ec) { call(ec); }
    MAKE_MOCK1(call, void(std::error_code));
};

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("request_handler: invalid request message") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    using trompeloeil::_;
    REQUIRE_CALL(handle_error,
                 call(std::error_code(common::errc::invalid_message)))
        .TIMES(1);
    REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

    CHECK(rh.handle_message(std::vector<std::uint8_t>{}));
}

TEST_CASE("request_handler: empty request message") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector(std::vector<flatbuffers::Offset<Request>>{})));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;
    REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));
    // No calls to 'write'.

    CHECK_FALSE(rh.handle_message(req_span));
}

TEST_CASE("request_handler: ping") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::PingRequest,
                             CreatePingRequest(b).Union()),
               CreateRequest(b, 43, AnyRequest::PingRequest,
                             CreatePingRequest(b).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;
    flatbuffers::DetachedBuffer resp_buf;
    REQUIRE_CALL(write, call(_))
        .LR_SIDE_EFFECT(resp_buf = std::move(_1))
        .TIMES(1);
    REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

    CHECK_FALSE(rh.handle_message(req_span));

    auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
    REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
    auto const *resp_msg =
        flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
    auto const *resps = resp_msg->responses();
    CHECK(resps->size() == 2);

    auto const *resp0 = resps->Get(0);
    CHECK(resp0->seqno() == 42);
    CHECK(resp0->status() == Status::OK);
    CHECK(resp0->response_type() == AnyResponse::PingResponse);

    auto const *resp1 = resps->Get(1);
    CHECK(resp1->seqno() == 43);
    CHECK(resp1->status() == Status::OK);
    CHECK(resp1->response_type() == AnyResponse::PingResponse);
}

TEST_CASE("request_handler: hello") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(
                   b, 42, AnyRequest::HelloRequest,
                   CreateHelloRequest(b, 123, b.CreateString("some_client"))
                       .Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("success") {
        REQUIRE_CALL(sess, hello("some_client", 123u, _, _))
            .SIDE_EFFECT(_3(7))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::HelloResponse);
        auto const *hello_resp = resp->response_as_HelloResponse();
        CHECK(hello_resp->conn_no() == 7);
    }

    SUBCASE("failure") {
        REQUIRE_CALL(sess, hello("some_client", 123u, _, _))
            .SIDE_EFFECT(_4(Status::INVALID_REQUEST))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        rh.handle_message(req_span);

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::INVALID_REQUEST);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

TEST_CASE("request_handler: quit") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::QuitRequest,
                             CreateQuitRequest(b).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;
    flatbuffers::DetachedBuffer resp_buf;
    REQUIRE_CALL(write, call(_))
        .LR_SIDE_EFFECT(resp_buf = std::move(_1))
        .TIMES(1);
    REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

    CHECK(rh.handle_message(req_span));
    auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
    REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
    auto const *resp_msg =
        flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
    auto const *resps = resp_msg->responses();
    CHECK(resps->size() == 1);
    auto const *resp = resps->Get(0);
    CHECK(resp->seqno() == 42);
    CHECK(resp->status() == Status::OK);
    CHECK(resp->response_type() == AnyResponse::QuitResponse);
}

TEST_CASE("request_handler: get_segment") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::GetSegmentRequest,
                             CreateGetSegmentRequest(b, 7).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("posix_mmap") {
        auto spec = segment_spec{posix_mmap_segment_spec{"/myshmem"}, 16384};
        REQUIRE_CALL(sess, get_segment(7u, _, _))
            .LR_SIDE_EFFECT(_2(spec))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::GetSegmentResponse);
        auto const *seg = resp->response_as_GetSegmentResponse()->segment();
        CHECK(seg->size() == 16384);
        CHECK(seg->spec_type() == SegmentMappingSpec::PosixMmapSpec);
        auto const *mapping = seg->spec_as_PosixMmapSpec();
        CHECK(mapping->name()->str() == "/myshmem");
        CHECK(mapping->use_shm_open());
    }

    SUBCASE("file_mmap") {
        auto spec = segment_spec{file_mmap_segment_spec{"/tmp/myfile"}, 16384};
        REQUIRE_CALL(sess, get_segment(7u, _, _))
            .LR_SIDE_EFFECT(_2(spec))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::GetSegmentResponse);
        auto const *seg = resp->response_as_GetSegmentResponse()->segment();
        CHECK(seg->size() == 16384);
        CHECK(seg->spec_type() == SegmentMappingSpec::PosixMmapSpec);
        auto const *mapping = seg->spec_as_PosixMmapSpec();
        CHECK(mapping->name()->str() == "/tmp/myfile");
        CHECK_FALSE(mapping->use_shm_open());
    }

    SUBCASE("sysv") {
        auto spec = segment_spec{sysv_segment_spec{1234}, 16384};
        REQUIRE_CALL(sess, get_segment(7u, _, _))
            .LR_SIDE_EFFECT(_2(spec))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::GetSegmentResponse);
        auto const *seg = resp->response_as_GetSegmentResponse()->segment();
        CHECK(seg->size() == 16384);
        CHECK(seg->spec_type() == SegmentMappingSpec::SystemVSharedMemorySpec);
        auto const *mapping = seg->spec_as_SystemVSharedMemorySpec();
        CHECK(mapping->shm_id() == 1234);
    }

    SUBCASE("win32") {
        auto spec =
            segment_spec{win32_segment_spec{"Local\\MyMapping", true}, 16384};
        REQUIRE_CALL(sess, get_segment(7u, _, _))
            .LR_SIDE_EFFECT(_2(spec))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::GetSegmentResponse);
        auto const *seg = resp->response_as_GetSegmentResponse()->segment();
        CHECK(seg->size() == 16384);
        CHECK(seg->spec_type() == SegmentMappingSpec::Win32FileMappingSpec);
        auto const *mapping = seg->spec_as_Win32FileMappingSpec();
        CHECK(mapping->name()->str() == "Local\\MyMapping");
        CHECK(mapping->use_large_pages());
    }

    SUBCASE("failure") {
        REQUIRE_CALL(sess, get_segment(7u, _, _))
            .SIDE_EFFECT(_3(Status::NO_SUCH_SEGMENT))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_SEGMENT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

TEST_CASE("request_handler: alloc") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::AllocRequest,
                             CreateAllocRequest(b, 1000).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("success") {
        auto const rsrc = mock_resource{7, 4096, 1024};
        REQUIRE_CALL(sess, alloc(1000, Policy::DEFAULT, _, _))
            .SIDE_EFFECT(_3(common::token(12345), rsrc))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::AllocResponse);
        auto const *alloc_resp = resp->response_as_AllocResponse();
        CHECK(alloc_resp->object()->key() == 12345);
        CHECK(alloc_resp->object()->segment() == 7);
        CHECK(alloc_resp->object()->offset() == 4096);
        CHECK(alloc_resp->object()->size() == 1024);
        CHECK_FALSE(alloc_resp->zeroed()); // Currently fixed
    }

    SUBCASE("failure") {
        REQUIRE_CALL(sess, alloc(1000, Policy::DEFAULT, _, _))
            .SIDE_EFFECT(_4(Status::OUT_OF_SHMEM))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OUT_OF_SHMEM);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

TEST_CASE("request_handler: open") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::OpenRequest,
                             CreateOpenRequest(b, 12345).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("immediate_success") {
        auto const rsrc = mock_resource{7, 4096, 1024};
        REQUIRE_CALL(sess, open(common::token(12345), Policy::DEFAULT, true, _,
                                _, _, _, _))
            .SIDE_EFFECT(_5(common::token(23456), rsrc))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::OpenResponse);
        auto const *open_resp = resp->response_as_OpenResponse();
        CHECK(open_resp->object()->key() == 23456);
        CHECK(open_resp->object()->segment() == 7);
        CHECK(open_resp->object()->offset() == 4096);
        CHECK(open_resp->object()->size() == 1024);
    }

    SUBCASE("immediate_failure") {
        REQUIRE_CALL(sess, open(common::token(12345), Policy::DEFAULT, true, _,
                                _, _, _, _))
            .SIDE_EFFECT(_6(Status::NO_SUCH_OBJECT))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_OBJECT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }

    SUBCASE("deferred_success") {
        std::function<void(common::token, mock_resource const &)>
            deferred_success_cb;
        REQUIRE_CALL(sess, open(common::token(12345), Policy::DEFAULT, true, _,
                                _, _, _, _))
            .LR_SIDE_EFFECT(deferred_success_cb = _7)
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);

        auto const rsrc = mock_resource{7, 4096, 1024};
        deferred_success_cb(common::token(23456), rsrc);

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::OpenResponse);
        auto const *open_resp = resp->response_as_OpenResponse();
        CHECK(open_resp->object()->key() == 23456);
        CHECK(open_resp->object()->segment() == 7);
        CHECK(open_resp->object()->offset() == 4096);
        CHECK(open_resp->object()->size() == 1024);
    }

    SUBCASE("deferred_failure") {
        std::function<void(Status)> deferred_error_cb;
        REQUIRE_CALL(sess, open(common::token(12345), Policy::DEFAULT, true, _,
                                _, _, _, _))
            .LR_SIDE_EFFECT(deferred_error_cb = _8)
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);

        deferred_error_cb(Status::NO_SUCH_OBJECT);

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_OBJECT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

TEST_CASE("request_handler: close") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::CloseRequest,
                             CreateCloseRequest(b, 12345).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("success") {
        REQUIRE_CALL(sess, close(common::token(12345), _, _))
            .SIDE_EFFECT(_2())
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::CloseResponse);
    }

    SUBCASE("immediate_failure") {
        REQUIRE_CALL(sess, close(common::token(12345), _, _))
            .SIDE_EFFECT(_3(Status::NO_SUCH_OBJECT))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_OBJECT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

TEST_CASE("request_handler: share") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::ShareRequest,
                             CreateShareRequest(b, 12345).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("success") {
        REQUIRE_CALL(sess, share(common::token(12345), _, _))
            .SIDE_EFFECT(_2())
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::ShareResponse);
    }

    SUBCASE("failure") {
        REQUIRE_CALL(sess, share(common::token(12345), _, _))
            .SIDE_EFFECT(_3(Status::NO_SUCH_OBJECT))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_OBJECT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

TEST_CASE("request_handler: unshare") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::UnshareRequest,
                             CreateUnshareRequest(b, 12345).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("immediate_success") {
        REQUIRE_CALL(sess, unshare(common::token(12345), true, _, _, _, _))
            .SIDE_EFFECT(_3(common::token(23456)))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::UnshareResponse);
        auto const *unshare_resp = resp->response_as_UnshareResponse();
        CHECK(unshare_resp->key() == 23456);
        CHECK_FALSE(unshare_resp->zeroed()); // Fixed for now
    }

    SUBCASE("immediate_failure") {
        REQUIRE_CALL(sess, unshare(common::token(12345), true, _, _, _, _))
            .SIDE_EFFECT(_4(Status::NO_SUCH_OBJECT))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_OBJECT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }

    SUBCASE("deferred_success") {
        std::function<void(common::token)> deferred_success_cb;
        REQUIRE_CALL(sess, unshare(common::token(12345), true, _, _, _, _))
            .LR_SIDE_EFFECT(deferred_success_cb = _5)
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);

        deferred_success_cb(common::token(23456));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::UnshareResponse);
        auto const *unshare_resp = resp->response_as_UnshareResponse();
        CHECK(unshare_resp->key() == 23456);
        CHECK_FALSE(unshare_resp->zeroed());
    }

    SUBCASE("deferred_failure") {
        std::function<void(Status)> deferred_error_cb;
        REQUIRE_CALL(sess, unshare(common::token(12345), true, _, _, _, _))
            .LR_SIDE_EFFECT(deferred_error_cb = _6)
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);

        deferred_error_cb(Status::NO_SUCH_OBJECT);

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_OBJECT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

TEST_CASE("request_handler: create_voucher") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::CreateVoucherRequest,
                             CreateCreateVoucherRequest(b, 12345, 3).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("success") {
        REQUIRE_CALL(sess, create_voucher(common::token(12345), 3u, _, _, _))
            .SIDE_EFFECT(_4(common::token(23456)))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::CreateVoucherResponse);
        CHECK(resp->response_as_CreateVoucherResponse()->key() == 23456);
    }

    SUBCASE("failure") {
        REQUIRE_CALL(sess, create_voucher(common::token(12345), 3u, _, _, _))
            .SIDE_EFFECT(_5(Status::NO_SUCH_OBJECT))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_OBJECT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

TEST_CASE("request_handler: discard_voucher") {
    mock_session sess;
    mock_writer write;
    mock_error_handler handle_error;
    auto rh = request_handler<mock_session>(
        sess, std::reference_wrapper(write), [] {},
        std::reference_wrapper(handle_error));

    flatbuffers::FlatBufferBuilder b;
    using namespace protocol;
    b.FinishSizePrefixed(CreateRequestMessage(
        b, b.CreateVector({
               CreateRequest(b, 42, AnyRequest::DiscardVoucherRequest,
                             CreateDiscardVoucherRequest(b, 12345).Union()),
           })));
    auto req_span = b.GetBufferSpan();

    using trompeloeil::_;

    SUBCASE("success") {
        REQUIRE_CALL(sess, discard_voucher(common::token(12345), _, _, _))
            .SIDE_EFFECT(_3(common::token(23456)))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::OK);
        CHECK(resp->response_type() == AnyResponse::DiscardVoucherResponse);
        CHECK(resp->response_as_DiscardVoucherResponse()->key() == 23456);
    }

    SUBCASE("failure") {
        REQUIRE_CALL(sess, discard_voucher(common::token(12345), _, _, _))
            .SIDE_EFFECT(_4(Status::NO_SUCH_OBJECT))
            .TIMES(1);
        flatbuffers::DetachedBuffer resp_buf;
        REQUIRE_CALL(write, call(_))
            .LR_SIDE_EFFECT(resp_buf = std::move(_1))
            .TIMES(1);
        REQUIRE_CALL(sess, perform_housekeeping()).TIMES(AT_MOST(1));

        CHECK_FALSE(rh.handle_message(req_span));

        auto verif = flatbuffers::Verifier(resp_buf.data(), resp_buf.size());
        REQUIRE(verif.VerifySizePrefixedBuffer<ResponseMessage>(nullptr));
        auto const *resp_msg =
            flatbuffers::GetSizePrefixedRoot<ResponseMessage>(resp_buf.data());
        auto const *resps = resp_msg->responses();
        CHECK(resps->size() == 1);
        auto const *resp = resps->Get(0);
        CHECK(resp->seqno() == 42);
        CHECK(resp->status() == Status::NO_SUCH_OBJECT);
        CHECK(resp->response_type() == AnyResponse::NONE);
    }
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::daemon
