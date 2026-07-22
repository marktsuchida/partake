/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "requests.hpp"

#include "partake/errors.hpp"

#include "request_builder.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <flatbuffers/flatbuffers.h>
#include <gsl/span>

#include <cstdint>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace partake::client::internal {

namespace {

auto span_of(flatbuffers::DetachedBuffer const &buf)
    -> gsl::span<std::uint8_t const> {
    return {buf.data(), buf.size()};
}

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("requests: get_process_id") {
#ifdef _WIN32
    CHECK(get_process_id() == static_cast<std::uint32_t>(_getpid()));
#else
    CHECK(get_process_id() == static_cast<std::uint32_t>(getpid()));
#endif
}

TEST_CASE("requests: make_client_hello") {
    auto buf = make_client_hello("my-client");
    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(verifier.VerifySizePrefixedBuffer<protocol::ClientHelloMessage>(
        nullptr));
    auto const *hello =
        flatbuffers::GetSizePrefixedRoot<protocol::ClientHelloMessage>(
            buf.data());
    CHECK(hello->protocol_version() ==
          static_cast<std::uint32_t>(protocol::ProtocolVersion::CURRENT));
    CHECK(hello->pid() == get_process_id());
    REQUIRE(hello->name() != nullptr);
    CHECK(hello->name()->str() == "my-client");
}

TEST_CASE("requests: add_ping_request and add_quit_request") {
    auto rb = request_builder(2);
    add_ping_request(rb, 42);
    add_quit_request(rb, 43);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *requests = msg->requests();
    REQUIRE(requests != nullptr);
    REQUIRE(requests->size() == 2);
    CHECK(requests->Get(0)->seqno() == 42);
    CHECK(requests->Get(0)->request_type() ==
          protocol::AnyRequest::PingRequest);
    CHECK(requests->Get(1)->seqno() == 43);
    CHECK(requests->Get(1)->request_type() ==
          protocol::AnyRequest::QuitRequest);
}

TEST_CASE("requests: add_get_segment_request") {
    auto rb = request_builder(1);
    add_get_segment_request(rb, 7, 3);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *requests = msg->requests();
    REQUIRE(requests != nullptr);
    REQUIRE(requests->size() == 1);
    CHECK(requests->Get(0)->seqno() == 7);
    CHECK(requests->Get(0)->request_type() ==
          protocol::AnyRequest::GetSegmentRequest);
    CHECK(requests->Get(0)->request_as_GetSegmentRequest()->segment() == 3);
}

namespace {

// A ResponseMessage with a single response of the given union type.
template <typename MakeResponse>
auto make_single_response_message(protocol::AnyResponse type,
                                  MakeResponse make_response)
    -> flatbuffers::DetachedBuffer {
    auto fbb = flatbuffers::FlatBufferBuilder();
    auto resp = protocol::CreateResponse(fbb, 42, protocol::Status::OK, type,
                                         make_response(fbb));
    fbb.FinishSizePrefixed(protocol::CreateResponseMessage(
        fbb, fbb.CreateVector(std::vector{resp})));
    return fbb.Release();
}

auto single_response(flatbuffers::DetachedBuffer const &buf)
    -> protocol::Response const * {
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
            buf.data());
    return msg->responses()->Get(0);
}

} // namespace

TEST_CASE("requests: decoders accept the expected union member") {
    auto ping_buf = make_single_response_message(
        protocol::AnyResponse::PingResponse,
        [](auto &fbb) { return protocol::CreatePingResponse(fbb).Union(); });
    auto quit_buf = make_single_response_message(
        protocol::AnyResponse::QuitResponse,
        [](auto &fbb) { return protocol::CreateQuitResponse(fbb).Union(); });
    CHECK(verify_response_message(span_of(ping_buf)));
    CHECK(verify_response_message(span_of(quit_buf)));

    CHECK(decode_ping_response(*single_response(ping_buf)).has_value());
    CHECK(decode_quit_response(*single_response(quit_buf)).has_value());

    // Wrong union member.
    CHECK_FALSE(decode_ping_response(*single_response(quit_buf)).has_value());
    CHECK_FALSE(decode_quit_response(*single_response(ping_buf)).has_value());
}

TEST_CASE("requests: decoders reject a missing union member") {
    // An error response carries no union member (NONE).
    auto fbb = flatbuffers::FlatBufferBuilder();
    auto resp =
        protocol::CreateResponse(fbb, 42, protocol::Status::INVALID_REQUEST);
    fbb.FinishSizePrefixed(protocol::CreateResponseMessage(
        fbb, fbb.CreateVector(std::vector{resp})));
    auto buf = fbb.Release();
    REQUIRE(verify_response_message(span_of(buf)));
    CHECK_FALSE(decode_ping_response(*single_response(buf)).has_value());
    CHECK_FALSE(decode_quit_response(*single_response(buf)).has_value());
}

namespace {

// A GetSegmentResponse-carrying ResponseMessage for one SegmentMappingSpec
// arm.
auto make_get_segment_message(std::uint64_t size,
                              protocol::SegmentMappingSpec spec_type,
                              flatbuffers::Offset<void> spec_union_builder(
                                  flatbuffers::FlatBufferBuilder &))
    -> flatbuffers::DetachedBuffer {
    return make_single_response_message(
        protocol::AnyResponse::GetSegmentResponse, [&](auto &fbb) {
            auto spec = protocol::CreateSegmentSpec(fbb, size, spec_type,
                                                    spec_union_builder(fbb));
            return protocol::CreateGetSegmentResponse(fbb, spec).Union();
        });
}

} // namespace

TEST_CASE("requests: decode_get_segment_response") {
    SECTION("posix shm_open") {
        auto buf = make_get_segment_message(
            16384, protocol::SegmentMappingSpec::PosixMmapSpec,
            [](flatbuffers::FlatBufferBuilder &fbb) {
                return protocol::CreatePosixMmapSpec(
                           fbb, fbb.CreateString("/myshmem"), true)
                    .Union();
            });
        auto decoded = decode_get_segment_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        CHECK(decoded->size == 16384);
        REQUIRE(std::holds_alternative<posix_mmap_spec>(decoded->spec));
        auto const &s = std::get<posix_mmap_spec>(decoded->spec);
        CHECK(s.name == "/myshmem");
        CHECK(s.use_shm_open);
    }

    SECTION("posix file-backed") {
        auto buf = make_get_segment_message(
            16384, protocol::SegmentMappingSpec::PosixMmapSpec,
            [](flatbuffers::FlatBufferBuilder &fbb) {
                return protocol::CreatePosixMmapSpec(
                           fbb, fbb.CreateString("/tmp/myfile"), false)
                    .Union();
            });
        auto decoded = decode_get_segment_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        REQUIRE(std::holds_alternative<posix_mmap_spec>(decoded->spec));
        auto const &s = std::get<posix_mmap_spec>(decoded->spec);
        CHECK(s.name == "/tmp/myfile");
        CHECK_FALSE(s.use_shm_open);
    }

    SECTION("sysv") {
        auto buf = make_get_segment_message(
            16384, protocol::SegmentMappingSpec::SystemVSharedMemorySpec,
            [](flatbuffers::FlatBufferBuilder &fbb) {
                return protocol::CreateSystemVSharedMemorySpec(fbb, 1234)
                    .Union();
            });
        auto decoded = decode_get_segment_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        REQUIRE(std::holds_alternative<sysv_shmem_spec>(decoded->spec));
        CHECK(std::get<sysv_shmem_spec>(decoded->spec).shm_id == 1234);
    }

    SECTION("win32") {
        auto buf = make_get_segment_message(
            16384, protocol::SegmentMappingSpec::Win32FileMappingSpec,
            [](flatbuffers::FlatBufferBuilder &fbb) {
                return protocol::CreateWin32FileMappingSpec(
                           fbb, fbb.CreateString("Local\\MyMapping"), true)
                    .Union();
            });
        auto decoded = decode_get_segment_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        REQUIRE(std::holds_alternative<win32_mapping_spec>(decoded->spec));
        auto const &s = std::get<win32_mapping_spec>(decoded->spec);
        CHECK(s.name == "Local\\MyMapping");
        CHECK(s.use_large_pages);
    }

    SECTION("wrong response type") {
        auto ping = make_single_response_message(
            protocol::AnyResponse::PingResponse, [](auto &fbb) {
                return protocol::CreatePingResponse(fbb).Union();
            });
        CHECK_FALSE(
            decode_get_segment_response(*single_response(ping)).has_value());
    }
}

TEST_CASE("requests: to_protocol_policy") {
    CHECK(to_protocol_policy(policy::default_) == protocol::Policy::DEFAULT);
    CHECK(to_protocol_policy(policy::primitive) ==
          protocol::Policy::PRIMITIVE);
}

TEST_CASE("requests: add_alloc_request") {
    auto rb = request_builder(1);
    add_alloc_request(rb, 11, 4096, protocol::Policy::PRIMITIVE);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *req = msg->requests()->Get(0);
    CHECK(req->seqno() == 11);
    REQUIRE(req->request_type() == protocol::AnyRequest::AllocRequest);
    auto const *areq = req->request_as_AllocRequest();
    CHECK(areq->size() == 4096);
    CHECK(areq->policy() == protocol::Policy::PRIMITIVE);
}

TEST_CASE("requests: add_open_request") {
    auto const wait = GENERATE(false, true);
    auto rb = request_builder(1);
    add_open_request(rb, 12, 777, protocol::Policy::DEFAULT, wait);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *req = msg->requests()->Get(0);
    CHECK(req->seqno() == 12);
    REQUIRE(req->request_type() == protocol::AnyRequest::OpenRequest);
    auto const *oreq = req->request_as_OpenRequest();
    CHECK(oreq->key() == 777);
    CHECK(oreq->policy() == protocol::Policy::DEFAULT);
    CHECK(oreq->wait() == wait);
}

TEST_CASE("requests: add_close_request") {
    auto rb = request_builder(1);
    add_close_request(rb, 13, 888);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *req = msg->requests()->Get(0);
    CHECK(req->seqno() == 13);
    REQUIRE(req->request_type() == protocol::AnyRequest::CloseRequest);
    CHECK(req->request_as_CloseRequest()->key() == 888);
}

TEST_CASE("requests: add_share_request") {
    auto rb = request_builder(1);
    add_share_request(rb, 14, 999);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *req = msg->requests()->Get(0);
    CHECK(req->seqno() == 14);
    REQUIRE(req->request_type() == protocol::AnyRequest::ShareRequest);
    CHECK(req->request_as_ShareRequest()->key() == 999);
}

TEST_CASE("requests: add_unshare_request") {
    auto const wait = GENERATE(false, true);
    auto rb = request_builder(1);
    add_unshare_request(rb, 15, 1001, wait);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *req = msg->requests()->Get(0);
    CHECK(req->seqno() == 15);
    REQUIRE(req->request_type() == protocol::AnyRequest::UnshareRequest);
    auto const *ureq = req->request_as_UnshareRequest();
    CHECK(ureq->key() == 1001);
    CHECK(ureq->wait() == wait);
}

TEST_CASE("requests: add_create_voucher_request") {
    auto const count = GENERATE(std::uint32_t(1), std::uint32_t(5));
    auto rb = request_builder(1);
    add_create_voucher_request(rb, 16, 1002, count);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *req = msg->requests()->Get(0);
    CHECK(req->seqno() == 16);
    REQUIRE(req->request_type() == protocol::AnyRequest::CreateVoucherRequest);
    auto const *vreq = req->request_as_CreateVoucherRequest();
    CHECK(vreq->key() == 1002);
    CHECK(vreq->count() == count);
}

TEST_CASE("requests: add_discard_voucher_request") {
    auto rb = request_builder(1);
    add_discard_voucher_request(rb, 17, 1003);
    auto buf = rb.release_buffer();

    auto verifier = flatbuffers::Verifier(buf.data(), buf.size());
    REQUIRE(
        verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(nullptr));
    auto const *msg =
        flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(buf.data());
    auto const *req = msg->requests()->Get(0);
    CHECK(req->seqno() == 17);
    REQUIRE(req->request_type() ==
            protocol::AnyRequest::DiscardVoucherRequest);
    CHECK(req->request_as_DiscardVoucherRequest()->key() == 1003);
}

TEST_CASE("requests: decode_alloc_response") {
    SECTION("success") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::AllocResponse, [](auto &fbb) {
                protocol::Mapping const m(123, 7, 4096, 512);
                return protocol::CreateAllocResponse(fbb, &m, true).Union();
            });
        auto decoded = decode_alloc_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        CHECK(decoded->key == 123);
        CHECK(decoded->segment == 7);
        CHECK(decoded->offset == 4096);
        CHECK(decoded->size == 512);
        CHECK(decoded->zeroed);
    }

    SECTION("wrong union member") {
        auto ping = make_single_response_message(
            protocol::AnyResponse::PingResponse, [](auto &fbb) {
                return protocol::CreatePingResponse(fbb).Union();
            });
        CHECK_FALSE(decode_alloc_response(*single_response(ping)).has_value());
    }

    SECTION("absent object") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::AllocResponse, [](auto &fbb) {
                return protocol::CreateAllocResponse(fbb).Union();
            });
        CHECK_FALSE(decode_alloc_response(*single_response(buf)).has_value());
    }
}

TEST_CASE("requests: decode_open_response") {
    SECTION("success") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::OpenResponse, [](auto &fbb) {
                protocol::Mapping const m(124, 8, 128, 64);
                return protocol::CreateOpenResponse(fbb, &m).Union();
            });
        auto decoded = decode_open_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        CHECK(decoded->key == 124);
        CHECK(decoded->segment == 8);
        CHECK(decoded->offset == 128);
        CHECK(decoded->size == 64);
    }

    SECTION("wrong union member") {
        auto ping = make_single_response_message(
            protocol::AnyResponse::PingResponse, [](auto &fbb) {
                return protocol::CreatePingResponse(fbb).Union();
            });
        CHECK_FALSE(decode_open_response(*single_response(ping)).has_value());
    }

    SECTION("absent object") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::OpenResponse, [](auto &fbb) {
                return protocol::CreateOpenResponse(fbb).Union();
            });
        CHECK_FALSE(decode_open_response(*single_response(buf)).has_value());
    }
}

TEST_CASE("requests: decode_close_response") {
    auto buf = make_single_response_message(
        protocol::AnyResponse::CloseResponse,
        [](auto &fbb) { return protocol::CreateCloseResponse(fbb).Union(); });
    CHECK(decode_close_response(*single_response(buf)).has_value());

    auto ping = make_single_response_message(
        protocol::AnyResponse::PingResponse,
        [](auto &fbb) { return protocol::CreatePingResponse(fbb).Union(); });
    CHECK_FALSE(decode_close_response(*single_response(ping)).has_value());
}

TEST_CASE("requests: decode_share_response") {
    auto buf = make_single_response_message(
        protocol::AnyResponse::ShareResponse,
        [](auto &fbb) { return protocol::CreateShareResponse(fbb).Union(); });
    CHECK(decode_share_response(*single_response(buf)).has_value());

    auto ping = make_single_response_message(
        protocol::AnyResponse::PingResponse,
        [](auto &fbb) { return protocol::CreatePingResponse(fbb).Union(); });
    CHECK_FALSE(decode_share_response(*single_response(ping)).has_value());
}

TEST_CASE("requests: decode_unshare_response") {
    SECTION("success") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::UnshareResponse, [](auto &fbb) {
                return protocol::CreateUnshareResponse(fbb, 125, true).Union();
            });
        auto decoded = decode_unshare_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        CHECK(decoded->key == 125);
        CHECK(decoded->zeroed);
    }

    SECTION("wrong union member") {
        auto ping = make_single_response_message(
            protocol::AnyResponse::PingResponse, [](auto &fbb) {
                return protocol::CreatePingResponse(fbb).Union();
            });
        CHECK_FALSE(
            decode_unshare_response(*single_response(ping)).has_value());
    }

    SECTION("zero key") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::UnshareResponse, [](auto &fbb) {
                return protocol::CreateUnshareResponse(fbb, 0, false).Union();
            });
        CHECK_FALSE(
            decode_unshare_response(*single_response(buf)).has_value());
    }
}

TEST_CASE("requests: decode_create_voucher_response") {
    SECTION("success") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::CreateVoucherResponse, [](auto &fbb) {
                return protocol::CreateCreateVoucherResponse(fbb, 126).Union();
            });
        auto decoded = decode_create_voucher_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        CHECK(decoded->key == 126);
    }

    SECTION("wrong union member") {
        auto ping = make_single_response_message(
            protocol::AnyResponse::PingResponse, [](auto &fbb) {
                return protocol::CreatePingResponse(fbb).Union();
            });
        CHECK_FALSE(decode_create_voucher_response(*single_response(ping))
                        .has_value());
    }

    SECTION("zero key") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::CreateVoucherResponse, [](auto &fbb) {
                return protocol::CreateCreateVoucherResponse(fbb, 0).Union();
            });
        CHECK_FALSE(
            decode_create_voucher_response(*single_response(buf)).has_value());
    }
}

TEST_CASE("requests: decode_discard_voucher_response") {
    SECTION("success") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::DiscardVoucherResponse, [](auto &fbb) {
                return protocol::CreateDiscardVoucherResponse(fbb, 127)
                    .Union();
            });
        auto decoded = decode_discard_voucher_response(*single_response(buf));
        REQUIRE(decoded.has_value());
        CHECK(decoded->key == 127);
    }

    SECTION("wrong union member") {
        auto ping = make_single_response_message(
            protocol::AnyResponse::PingResponse, [](auto &fbb) {
                return protocol::CreatePingResponse(fbb).Union();
            });
        CHECK_FALSE(decode_discard_voucher_response(*single_response(ping))
                        .has_value());
    }

    SECTION("zero key") {
        auto buf = make_single_response_message(
            protocol::AnyResponse::DiscardVoucherResponse, [](auto &fbb) {
                return protocol::CreateDiscardVoucherResponse(fbb, 0).Union();
            });
        CHECK_FALSE(decode_discard_voucher_response(*single_response(buf))
                        .has_value());
    }
}

TEST_CASE("requests: error_code_for_status") {
    using s = protocol::Status;
    CHECK(error_code_for_status(s::OK) == std::error_code());
    CHECK(error_code_for_status(s::INVALID_REQUEST) ==
          protocol_errc::invalid_request);
    CHECK(error_code_for_status(s::OUT_OF_SHMEM) ==
          protocol_errc::out_of_shmem);
    CHECK(error_code_for_status(s::NO_SUCH_SEGMENT) ==
          protocol_errc::no_such_segment);
    CHECK(error_code_for_status(s::NO_SUCH_OBJECT) ==
          protocol_errc::no_such_object);
    CHECK(error_code_for_status(s::OBJECT_BUSY) == protocol_errc::object_busy);
    CHECK(error_code_for_status(s::OBJECT_RESERVED) ==
          protocol_errc::object_reserved);
    // Deliberate out-of-range value (well-defined: Status has a fixed
    // underlying type).
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    CHECK(error_code_for_status(static_cast<s>(1000)) ==
          protocol_errc::unknown_protocol_error);

    CHECK(
        std::string(error_code_for_status(s::OBJECT_BUSY).category().name()) ==
        "partake protocol");
}

TEST_CASE("requests: verify functions") {
    auto hello_ok = [] {
        auto fbb = flatbuffers::FlatBufferBuilder();
        fbb.FinishSizePrefixed(protocol::CreateServerHelloMessage(
            fbb, protocol::HelloResult::OK, 0, 42));
        return fbb.Release();
    }();
    CHECK(verify_server_hello_message(span_of(hello_ok)));

    // Valid framing (prefix covers the frame) but garbage payload.
    auto const garbage = std::vector<std::uint8_t>{
        12,   0,    0,    0,    0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    auto const garbage_span = gsl::span<std::uint8_t const>(garbage);
    CHECK_FALSE(verify_server_hello_message(garbage_span));
    CHECK_FALSE(verify_response_message(garbage_span));
    CHECK_FALSE(verify_server_hello_message({}));
    CHECK_FALSE(verify_response_message({}));
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::client::internal
