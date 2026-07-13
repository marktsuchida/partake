/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "requests.hpp"

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

} // namespace partake::client::internal
