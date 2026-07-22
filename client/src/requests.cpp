/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "requests.hpp"

#include <cassert>
#include <exception>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace partake::client::internal {

auto get_process_id() -> std::uint32_t {
#ifdef _WIN32
    return static_cast<std::uint32_t>(_getpid());
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

auto verify_server_hello_message(gsl::span<std::uint8_t const> bytes) -> bool {
    auto verifier = flatbuffers::Verifier(bytes.data(), bytes.size());
    return verifier.VerifySizePrefixedBuffer<protocol::ServerHelloMessage>(
        nullptr);
}

auto verify_response_message(gsl::span<std::uint8_t const> bytes) -> bool {
    auto verifier = flatbuffers::Verifier(bytes.data(), bytes.size());
    return verifier.VerifySizePrefixedBuffer<protocol::ResponseMessage>(
        nullptr);
}

auto error_code_for_status(protocol::Status status) noexcept
    -> std::error_code {
    switch (status) {
        using s = protocol::Status;
    case s::OK:
        return {};
    case s::INVALID_REQUEST:
        return protocol_errc::invalid_request;
    case s::OUT_OF_SHMEM:
        return protocol_errc::out_of_shmem;
    case s::NO_SUCH_SEGMENT:
        return protocol_errc::no_such_segment;
    case s::NO_SUCH_OBJECT:
        return protocol_errc::no_such_object;
    case s::OBJECT_BUSY:
        return protocol_errc::object_busy;
    case s::OBJECT_RESERVED:
        return protocol_errc::object_reserved;
    }
    return protocol_errc::unknown_protocol_error;
}

auto to_protocol_policy(policy pol) noexcept -> protocol::Policy {
    switch (pol) {
    case policy::default_:
        return protocol::Policy::DEFAULT;
    case policy::primitive:
        return protocol::Policy::PRIMITIVE;
    }
    assert(false);
    std::terminate();
}

auto make_client_hello(std::string_view name) -> flatbuffers::DetachedBuffer {
    auto fbb = flatbuffers::FlatBufferBuilder();
    fbb.FinishSizePrefixed(protocol::CreateClientHelloMessage(
        fbb, static_cast<std::uint32_t>(protocol::ProtocolVersion::CURRENT),
        get_process_id(), fbb.CreateString(name.data(), name.size())));
    return fbb.Release();
}

void add_ping_request(request_builder &rb, std::uint64_t seqno) {
    rb.add_request(seqno, protocol::CreatePingRequest(rb.fbbuilder()));
}

void add_quit_request(request_builder &rb, std::uint64_t seqno) {
    rb.add_request(seqno, protocol::CreateQuitRequest(rb.fbbuilder()));
}

void add_get_segment_request(request_builder &rb, std::uint64_t seqno,
                             std::uint32_t segment_no) {
    rb.add_request(
        seqno, protocol::CreateGetSegmentRequest(rb.fbbuilder(), segment_no));
}

void add_alloc_request(request_builder &rb, std::uint64_t seqno,
                       std::uint64_t size, protocol::Policy policy) {
    rb.add_request(seqno,
                   protocol::CreateAllocRequest(rb.fbbuilder(), size, policy));
}

void add_open_request(request_builder &rb, std::uint64_t seqno,
                      std::uint64_t key, protocol::Policy policy, bool wait) {
    rb.add_request(
        seqno, protocol::CreateOpenRequest(rb.fbbuilder(), key, policy, wait));
}

void add_close_request(request_builder &rb, std::uint64_t seqno,
                       std::uint64_t key) {
    rb.add_request(seqno, protocol::CreateCloseRequest(rb.fbbuilder(), key));
}

void add_share_request(request_builder &rb, std::uint64_t seqno,
                       std::uint64_t key) {
    rb.add_request(seqno, protocol::CreateShareRequest(rb.fbbuilder(), key));
}

void add_unshare_request(request_builder &rb, std::uint64_t seqno,
                         std::uint64_t key, bool wait) {
    rb.add_request(seqno,
                   protocol::CreateUnshareRequest(rb.fbbuilder(), key, wait));
}

auto decode_ping_response(protocol::Response const &resp)
    -> std::optional<ping_result> {
    if (resp.response_type() != protocol::AnyResponse::PingResponse)
        return std::nullopt;
    return ping_result{};
}

auto decode_quit_response(protocol::Response const &resp)
    -> std::optional<quit_result> {
    if (resp.response_type() != protocol::AnyResponse::QuitResponse)
        return std::nullopt;
    return quit_result{};
}

auto decode_get_segment_response(protocol::Response const &resp)
    -> std::optional<segment_spec> {
    if (resp.response_type() != protocol::AnyResponse::GetSegmentResponse)
        return std::nullopt;
    auto const *seg = resp.response_as_GetSegmentResponse()->segment();
    if (seg == nullptr)
        return std::nullopt;
    segment_spec result;
    result.size = seg->size();
    switch (seg->spec_type()) {
    case protocol::SegmentMappingSpec::PosixMmapSpec: {
        auto const *m = seg->spec_as_PosixMmapSpec();
        if (m->name() == nullptr)
            return std::nullopt;
        result.spec = posix_mmap_spec{m->name()->str(), m->use_shm_open()};
        return result;
    }
    case protocol::SegmentMappingSpec::SystemVSharedMemorySpec: {
        auto const *m = seg->spec_as_SystemVSharedMemorySpec();
        result.spec = sysv_shmem_spec{m->shm_id()};
        return result;
    }
    case protocol::SegmentMappingSpec::Win32FileMappingSpec: {
        auto const *m = seg->spec_as_Win32FileMappingSpec();
        if (m->name() == nullptr)
            return std::nullopt;
        result.spec =
            win32_mapping_spec{m->name()->str(), m->use_large_pages()};
        return result;
    }
    case protocol::SegmentMappingSpec::NONE:
        break;
    }
    return std::nullopt;
}

auto decode_alloc_response(protocol::Response const &resp)
    -> std::optional<alloc_result> {
    if (resp.response_type() != protocol::AnyResponse::AllocResponse)
        return std::nullopt;
    auto const *r = resp.response_as_AllocResponse();
    auto const *obj = r->object();
    if (obj == nullptr) // Status was OK, so a null Mapping is a violation.
        return std::nullopt;
    return alloc_result{obj->key(), obj->segment(), obj->offset(), obj->size(),
                        r->zeroed()};
}

auto decode_open_response(protocol::Response const &resp)
    -> std::optional<open_result> {
    if (resp.response_type() != protocol::AnyResponse::OpenResponse)
        return std::nullopt;
    auto const *obj = resp.response_as_OpenResponse()->object();
    if (obj == nullptr)
        return std::nullopt;
    return open_result{obj->key(), obj->segment(), obj->offset(), obj->size()};
}

auto decode_close_response(protocol::Response const &resp)
    -> std::optional<close_result> {
    if (resp.response_type() != protocol::AnyResponse::CloseResponse)
        return std::nullopt;
    return close_result{};
}

auto decode_share_response(protocol::Response const &resp)
    -> std::optional<share_result> {
    if (resp.response_type() != protocol::AnyResponse::ShareResponse)
        return std::nullopt;
    return share_result{};
}

auto decode_unshare_response(protocol::Response const &resp)
    -> std::optional<unshare_result> {
    if (resp.response_type() != protocol::AnyResponse::UnshareResponse)
        return std::nullopt;
    auto const *r = resp.response_as_UnshareResponse();
    if (r->key() == 0) // Status was OK, so a zero key is a violation.
        return std::nullopt;
    return unshare_result{r->key(), r->zeroed()};
}

} // namespace partake::client::internal
