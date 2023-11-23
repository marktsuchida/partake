/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "errors.hpp"
#include "overloaded.hpp"
#include "partake_protocol_generated.h"
#include "response_builder.hpp"
#include "segment.hpp"
#include "time_point.hpp"
#include "token.hpp"

#include <gsl/pointers>
#include <gsl/span>

#include <cstdint>
#include <functional>
#include <utility>

namespace partake::daemon {

namespace internal {

inline auto segment_spec_to_fb(flatbuffers::FlatBufferBuilder &fbb,
                               segment_spec const &spec) {
    auto [seg_type, seg_mapping_spec] = std::visit(
        common::overloaded{
            [&fbb](posix_mmap_segment_spec const &s) {
                return std::make_pair(
                    protocol::SegmentMappingSpec::PosixMmapSpec,
                    protocol::CreatePosixMmapSpec(
                        fbb, fbb.CreateString(s.name), true)
                        .Union());
            },
            [&fbb](file_mmap_segment_spec const &s) {
                return std::make_pair(
                    protocol::SegmentMappingSpec::PosixMmapSpec,
                    protocol::CreatePosixMmapSpec(
                        fbb, fbb.CreateString(s.filename), false)
                        .Union());
            },
            [&fbb](sysv_segment_spec const &s) {
                return std::make_pair(
                    protocol::SegmentMappingSpec::SystemVSharedMemorySpec,
                    protocol::CreateSystemVSharedMemorySpec(fbb, s.shm_id)
                        .Union());
            },
            [&fbb](win32_segment_spec const &s) {
                return std::make_pair(
                    protocol::SegmentMappingSpec::Win32FileMappingSpec,
                    protocol::CreateWin32FileMappingSpec(
                        fbb, fbb.CreateString(s.name), s.use_large_pages)
                        .Union());
            },
        },
        spec.spec);

    return protocol::CreateSegmentSpec(fbb, spec.size, seg_type,
                                       seg_mapping_spec);
}

template <typename Resource>
inline auto make_mapping(common::token key, Resource const &rsrc)
    -> protocol::Mapping {
    return protocol::Mapping(key.as_u64(), rsrc.segment_id(), rsrc.offset(),
                             rsrc.size());
}

inline auto verify_request_message(gsl::span<std::uint8_t const> bytes)
    -> bool {
    auto verifier = flatbuffers::Verifier(bytes.data(), bytes.size());
    return verifier.VerifySizePrefixedBuffer<protocol::RequestMessage>(
        nullptr);
}

} // namespace internal

template <typename Session> class request_handler {
    gsl::not_null<Session *> sess;
    std::function<void(flatbuffers::DetachedBuffer &&)> write_resp;
    std::function<void()> housekeep;
    std::function<void(std::error_code)> handle_err; // Fatal message errors

    using resource_type = typename Session::object_type::resource_type;

  public:
    explicit request_handler(
        Session &session,
        std::function<void(flatbuffers::DetachedBuffer &&)> write_response,
        std::function<void()> per_request_housekeeping,
        std::function<void(std::error_code)> handle_error)
        : sess(&session), write_resp(std::move(write_response)),
          housekeep(std::move(per_request_housekeeping)),
          handle_err(std::move(handle_error)) {}

    // No move or copy (reference taken by handlers)
    ~request_handler() = default;
    request_handler(request_handler const &) = delete;
    auto operator=(request_handler const &) = delete;
    request_handler(request_handler &&) = delete;
    auto operator=(request_handler &&) = delete;

    // Deserialize and handle one FlatBuffers message.
    auto handle_message(gsl::span<std::uint8_t const> bytes) -> bool {
        if (not internal::verify_request_message(bytes)) {
            handle_err(std::error_code(common::errc::invalid_message));
            return true;
        }
        auto const *req_msg =
            flatbuffers::GetSizePrefixedRoot<protocol::RequestMessage>(
                bytes.data());
        auto const *requests = req_msg->requests();
        auto rb = response_builder(requests->size());
        auto now = clock::now();

        bool done = false;
        for (auto const *req : *requests) {
            auto type = req->request_type();
            if (type >= protocol::AnyRequest::MIN &&
                type <= protocol::AnyRequest::MAX) {
                done = handle_request(req, now, rb);
            } else {
                // From here on, we can meaningfully report errors to the
                // client. However, unexpected request type is a client bug, so
                // we do end the session.
                rb.add_error_response(req->seqno(),
                                      protocol::Status::INVALID_REQUEST);
                handle_err(
                    std::error_code(common::errc::invalid_request_type));
                done = true;
            }
            if (done)
                break;
        }

        if (not rb.empty()) {
            write_resp(rb.release_buffer());
        }

        // Rehash tables at most once per request message, after having
        // kicked off responses (to increase the chance that it is done
        // when the daemon is otherwise idle).
        sess->perform_housekeeping();
        housekeep();

        return done;
    }

  private:
    auto handle_request(protocol::Request const *req, time_point now,
                        response_builder &rb) -> bool {
        auto seqno = req->seqno();
        auto type = req->request_type();
        switch (type) {
            using r = protocol::AnyRequest;
        case r::PingRequest:
            return handle_ping(seqno, req->request_as_PingRequest(), rb);
        case r::HelloRequest:
            return handle_hello(seqno, req->request_as_HelloRequest(), rb);
        case r::QuitRequest:
            return handle_quit(seqno, req->request_as_QuitRequest(), rb);
        case r::GetSegmentRequest:
            return handle_get_segment(seqno,
                                      req->request_as_GetSegmentRequest(), rb);
        case r::AllocRequest:
            return handle_alloc(seqno, req->request_as_AllocRequest(), rb);
        case r::OpenRequest:
            return handle_open(seqno, req->request_as_OpenRequest(), now, rb);
        case r::CloseRequest:
            return handle_close(seqno, req->request_as_CloseRequest(), rb);
        case r::ShareRequest:
            return handle_share(seqno, req->request_as_ShareRequest(), rb);
        case r::UnshareRequest:
            return handle_unshare(seqno, req->request_as_UnshareRequest(), rb);
        case r::CreateVoucherRequest:
            return handle_create_voucher(
                seqno, req->request_as_CreateVoucherRequest(), now, rb);
        case r::DiscardVoucherRequest:
            return handle_discard_voucher(
                seqno, req->request_as_DiscardVoucherRequest(), now, rb);
        default:
            assert(false); // Forgot to implement
            std::terminate();
        }
    }

    auto handle_ping(std::uint64_t seqno, protocol::PingRequest const *req,
                     response_builder &rb) -> bool {
        (void)req;
        auto &fbb = rb.fbbuilder();
        auto resp = protocol::CreatePingResponse(fbb);
        rb.add_successful_response(seqno, resp);
        return false;
    }

    auto handle_hello(std::uint64_t seqno, protocol::HelloRequest const *req,
                      response_builder &rb) -> bool {
        auto const *name = req->name();
        sess->hello(
            {name->c_str(), name->size()}, req->pid(),
            [seqno, &rb](std::uint32_t session_id) {
                auto &fbb = rb.fbbuilder();
                auto resp = protocol::CreateHelloResponse(fbb, session_id);
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            });
        return false;
    }

    auto handle_quit(std::uint64_t seqno, protocol::QuitRequest const *req,
                     response_builder &rb) -> bool {
        (void)req;
        auto &fbb = rb.fbbuilder();
        auto resp = protocol::CreateQuitResponse(fbb);
        rb.add_successful_response(seqno, resp);
        return true; // End of read stream.
    }

    auto handle_get_segment(std::uint64_t seqno,
                            protocol::GetSegmentRequest const *req,
                            response_builder &rb) -> bool {
        sess->get_segment(
            req->segment(),
            [seqno, &rb](segment_spec const &spec) {
                auto &fbb = rb.fbbuilder();
                auto seg_spec = internal::segment_spec_to_fb(fbb, spec);
                auto resp = protocol::CreateGetSegmentResponse(fbb, seg_spec);
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            });
        return false;
    }

    auto handle_alloc(std::uint64_t seqno, protocol::AllocRequest const *req,
                      response_builder &rb) -> bool {
        sess->alloc(
            req->size(), req->policy(),
            [seqno, &rb](common::token k, resource_type const &rsrc) {
                auto &fbb = rb.fbbuilder();
                auto mapping = internal::make_mapping(k, rsrc);
                auto resp = protocol::CreateAllocResponse(fbb, &mapping);
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            });
        return false;
    }

    auto handle_open(std::uint64_t seqno, protocol::OpenRequest const *req,
                     time_point now, response_builder &rb) -> bool {
        sess->open(
            common::token(req->key()), req->policy(), req->wait(), now,
            [seqno, &rb](common::token k, resource_type const &rsrc) {
                auto &fbb = rb.fbbuilder();
                auto mapping = internal::make_mapping(k, rsrc);
                auto resp = protocol::CreateOpenResponse(fbb, &mapping);
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            },
            [seqno, this](common::token k, resource_type const &rsrc) {
                auto rb2 = response_builder(1);
                auto &fbb = rb2.fbbuilder();
                auto mapping = internal::make_mapping(k, rsrc);
                auto resp = protocol::CreateOpenResponse(fbb, &mapping);
                rb2.add_successful_response(seqno, resp);
                write_resp(rb2.release_buffer());
            },
            [seqno, this](protocol::Status status) {
                auto rb2 = response_builder(1);
                rb2.add_error_response(seqno, status);
                write_resp(rb2.release_buffer());
            });
        return false;
    }

    auto handle_close(std::uint64_t seqno, protocol::CloseRequest const *req,
                      response_builder &rb) -> bool {
        sess->close(
            common::token(req->key()),
            [seqno, &rb]() {
                auto &fbb = rb.fbbuilder();
                auto resp = protocol::CreateCloseResponse(fbb);
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            });
        return false;
    }

    auto handle_share(std::uint64_t seqno, protocol::ShareRequest const *req,
                      response_builder &rb) -> bool {
        sess->share(
            common::token(req->key()),
            [seqno, &rb]() {
                auto &fbb = rb.fbbuilder();
                auto resp = protocol::CreateShareResponse(fbb);
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            });
        return false;
    }

    auto handle_unshare(std::uint64_t seqno,
                        protocol::UnshareRequest const *req,
                        response_builder &rb) -> bool {
        sess->unshare(
            common::token(req->key()), req->wait(),
            [seqno, &rb](common::token new_key) {
                auto &fbb = rb.fbbuilder();
                auto resp =
                    protocol::CreateUnshareResponse(fbb, new_key.as_u64());
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            },
            [seqno, this](common::token new_key) {
                auto rb2 = response_builder(1);
                auto &fbb = rb2.fbbuilder();
                auto resp =
                    protocol::CreateUnshareResponse(fbb, new_key.as_u64());
                rb2.add_successful_response(seqno, resp);
                write_resp(rb2.release_buffer());
            },
            [seqno, this](protocol::Status status) {
                auto rb2 = response_builder(1);
                rb2.add_error_response(seqno, status);
                write_resp(rb2.release_buffer());
            });
        return false;
    }

    auto handle_create_voucher(std::uint64_t seqno,
                               protocol::CreateVoucherRequest const *req,
                               time_point now, response_builder &rb) -> bool {
        sess->create_voucher(
            common::token(req->key()), req->count(), now,
            [seqno, &rb](common::token voucher_key) {
                auto &fbb = rb.fbbuilder();
                auto resp = protocol::CreateCreateVoucherResponse(
                    fbb, voucher_key.as_u64());
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            });
        return false;
    }

    auto handle_discard_voucher(std::uint64_t seqno,
                                protocol::DiscardVoucherRequest const *req,
                                time_point now, response_builder &rb) -> bool {
        sess->discard_voucher(
            common::token(req->key()), now,
            [seqno, &rb](common::token object_key) {
                auto &fbb = rb.fbbuilder();
                auto resp = protocol::CreateDiscardVoucherResponse(
                    fbb, object_key.as_u64());
                rb.add_successful_response(seqno, resp);
            },
            [seqno, &rb](protocol::Status status) {
                rb.add_error_response(seqno, status);
            });
        return false;
    }
};

} // namespace partake::daemon
