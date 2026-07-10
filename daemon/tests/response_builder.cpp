/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "response_builder.hpp"

#include "framing.hpp"

#include <catch2/catch_test_macros.hpp>

#include <gsl/span>

#include <cstdint>
#include <vector>

namespace partake::daemon {

namespace {

[[maybe_unused]] auto
verify_response_message(gsl::span<std::uint8_t const> bytes) -> bool {
    auto verifier = flatbuffers::Verifier(bytes.data(), bytes.size());
    return verifier.VerifySizePrefixedBuffer<protocol::ResponseMessage>(
        nullptr);
}

struct buffer_collector {
    std::vector<flatbuffers::DetachedBuffer> buffers;

    auto sink() {
        return [this](flatbuffers::DetachedBuffer &&buf) {
            buffers.push_back(std::move(buf));
        };
    }

    [[nodiscard]] auto bytes(std::size_t i) const
        -> gsl::span<std::uint8_t const> {
        return {buffers[i].data(), buffers[i].size()};
    }
};

} // namespace

TEST_CASE("response_builder: flush with no responses emits nothing") {
    buffer_collector bc;
    response_builder rb(bc.sink());
    CHECK(rb.empty());
    rb.flush();
    CHECK(bc.buffers.empty());
}

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("response_builder: successful response") {
    buffer_collector bc;
    response_builder rb(bc.sink());
    auto &fbb = rb.fbbuilder();
    auto resp = protocol::CreatePingResponse(fbb);
    rb.add_successful_response(123, resp);
    CHECK_FALSE(rb.empty());
    rb.flush();
    CHECK(rb.empty());
    REQUIRE(bc.buffers.size() == 1);
    auto const bytes = bc.bytes(0);

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
    buffer_collector bc;
    response_builder rb(bc.sink());
    rb.add_error_response(123, protocol::Status::INVALID_REQUEST);
    CHECK_FALSE(rb.empty());
    rb.flush();
    REQUIRE(bc.buffers.size() == 1);
    auto const bytes = bc.bytes(0);

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

TEST_CASE("response_builder: rotates before exceeding max frame size") {
    static constexpr std::size_t n_responses = 4000;

    buffer_collector bc;
    response_builder rb(bc.sink());
    for (std::uint64_t seqno = 0; seqno < n_responses; ++seqno)
        rb.add_error_response(seqno, protocol::Status::INVALID_REQUEST);
    rb.flush();

    CHECK(bc.buffers.size() > 1);
    std::vector<std::uint64_t> seqnos;
    for (std::size_t i = 0; i < bc.buffers.size(); ++i) {
        auto const bytes = bc.bytes(i);
        REQUIRE(verify_response_message(bytes));
        CHECK(bytes.size() <= common::max_message_frame_len);
        auto const *root =
            flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
                bytes.data());
        CHECK(root->responses()->size() > 0);
        for (auto const *resp : *root->responses())
            seqnos.push_back(resp->seqno());
    }

    REQUIRE(seqnos.size() == n_responses);
    for (std::uint64_t seqno = 0; seqno < n_responses; ++seqno)
        CHECK(seqnos[seqno] == seqno);
}

TEST_CASE("response_builder: flush is idempotent") {
    buffer_collector bc;
    response_builder rb(bc.sink());
    rb.add_error_response(42, protocol::Status::NO_SUCH_OBJECT);
    rb.flush();
    CHECK(bc.buffers.size() == 1);
    rb.flush();
    CHECK(bc.buffers.size() == 1);
}

TEST_CASE("response_builder: usable after flush") {
    buffer_collector bc;
    response_builder rb(bc.sink());
    rb.add_error_response(1, protocol::Status::NO_SUCH_OBJECT);
    rb.flush();
    rb.add_error_response(2, protocol::Status::NO_SUCH_OBJECT);
    rb.flush();

    REQUIRE(bc.buffers.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        auto const bytes = bc.bytes(i);
        REQUIRE(verify_response_message(bytes));
        auto const *root =
            flatbuffers::GetSizePrefixedRoot<protocol::ResponseMessage>(
                bytes.data());
        REQUIRE(root->responses()->size() == 1);
        CHECK(root->responses()->Get(0)->seqno() == i + 1);
    }
}

// NOLINTEND(readability-magic-numbers)

} // namespace partake::daemon
