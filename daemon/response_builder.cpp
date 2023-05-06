/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "response_builder.hpp"

#include <doctest.h>
#include <gsl/span>

namespace partake::daemon {

namespace {

[[maybe_unused]] auto
verify_response_message(gsl::span<std::uint8_t const> bytes) -> bool {
    auto verifier = flatbuffers::Verifier(bytes.data(), bytes.size());
    return verifier.VerifySizePrefixedBuffer<protocol::ResponseMessage>(
        nullptr);
}

} // namespace

TEST_CASE("response_builder: empty response") {
    response_builder rb;
    CHECK(rb.empty());
    auto buf = rb.release_buffer();
    gsl::span<std::uint8_t const> const bytes(buf.data(), buf.size());

    CHECK(verify_response_message(bytes));
    auto const *root =
        flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
            bytes.data());
    CHECK(root->responses()->size() == 0);
}

TEST_CASE("response_builder: successful response") {
    response_builder rb;
    auto &fbb = rb.fbbuilder();
    auto resp = protocol::CreatePingResponse(fbb);
    rb.add_successful_response(123, resp);
    CHECK_FALSE(rb.empty());
    auto buf = rb.release_buffer();
    gsl::span<std::uint8_t const> const bytes(buf.data(), buf.size());

    CHECK(verify_response_message(bytes));
    auto const *root =
        flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
            bytes.data());
    CHECK(root->responses()->size() == 1);
    auto const *resp0 = root->responses()->Get(0);
    CHECK(resp0->seqno() == 123);
    CHECK(resp0->status() == protocol::Status::OK);
    CHECK(resp0->response_type() == protocol::AnyResponse::PingResponse);
}

TEST_CASE("response_builder: error response") {
    response_builder rb;
    rb.add_error_response(123, protocol::Status::INVALID_REQUEST);
    CHECK_FALSE(rb.empty());
    auto buf = rb.release_buffer();
    gsl::span<std::uint8_t const> const bytes(buf.data(), buf.size());

    CHECK(verify_response_message(bytes));
    auto const *root =
        flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
            bytes.data());
    CHECK(root->responses()->size() == 1);
    auto const *resp0 = root->responses()->Get(0);
    CHECK(resp0->seqno() == 123);
    CHECK(resp0->status() == protocol::Status::INVALID_REQUEST);
    CHECK(resp0->response_type() == protocol::AnyResponse::NONE);
}

} // namespace partake::daemon
