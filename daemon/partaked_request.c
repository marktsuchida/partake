/*
 * Partake request accessor functions
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_malloc.h"
#include "partaked_request.h"

#include "partake_protocol_verifier.h"

#include <zf_log.h>

#include <stdlib.h>

#define MESSAGE_FRAME_ALIGNMENT 8

struct partaked_reqarray {
    struct partaked_iobuf *iobuf; // Owning, reference-counted
    size_t offset;                // Excluding 32-bit size prefix

    partake_protocol_RequestMessage_table_t table;
};

struct partaked_request {
    struct partaked_iobuf *iobuf; // Owning, reference-counted

    partake_protocol_Request_table_t table;
};

bool partaked_requestframe_scan(struct partaked_iobuf *iobuf, size_t start,
                                size_t size, size_t *frame_size,
                                bool *frame_complete) {
    if (size < sizeof(flatbuffers_uoffset_t)) {
        *frame_size = 0;
        *frame_complete = false;
        return size > 0;
    }

    flatbuffers_read_size_prefix((char *)iobuf->buffer + start, frame_size);

    *frame_size += sizeof(flatbuffers_uoffset_t);

    *frame_size = (*frame_size + MESSAGE_FRAME_ALIGNMENT - 1) &
                  ~(MESSAGE_FRAME_ALIGNMENT - 1);

    *frame_complete = *frame_size <= size;
    return true;
}

struct partaked_iobuf *
partaked_requestframe_maybe_move(struct partaked_iobuf *iobuf, size_t *start,
                                 size_t size) {
    // For now we just always copy unless already at start. TODO Avoid moving
    // when size is large-ish but there is still plenty of room left in iobuf.
    if (*start > 0) {
        struct partaked_iobuf *newbuf =
            partaked_iobuf_create(PARTAKED_IOBUF_STD_SIZE);
        memcpy(newbuf->buffer, (char *)iobuf->buffer + *start, size);
        partaked_iobuf_decref(iobuf);
        *start = 0;
        return newbuf;
    }
    return iobuf;
}

struct partaked_reqarray *partaked_reqarray_create(struct partaked_iobuf *iobuf,
                                                   size_t offset) {
    size_t size;
    void *msg =
        flatbuffers_read_size_prefix((char *)iobuf->buffer + offset, &size);
    if (size > (32 << 10) - sizeof(flatbuffers_uoffset_t)) {
        ZF_LOGE("Request too long");
        return NULL;
    }

    int err = partake_protocol_RequestMessage_verify_as_root(msg, size);
    if (err != 0) {
        ZF_LOGE("Request verification failed: %s",
                flatcc_verify_error_string(err));
        return NULL;
    }

    struct partaked_reqarray *reqarr = partaked_malloc(sizeof(*reqarr));
    reqarr->iobuf = partaked_iobuf_incref(iobuf);
    reqarr->offset = offset;
    reqarr->table = partake_protocol_RequestMessage_as_root(msg);

    return reqarr;
}

void partaked_reqarray_destroy(struct partaked_reqarray *reqarr) {
    if (reqarr == NULL)
        return;

    partaked_iobuf_decref(reqarr->iobuf);
    partaked_free(reqarr);
}

uint32_t partaked_reqarray_count(struct partaked_reqarray *reqarr) {
    return partake_protocol_Request_vec_len(
        partake_protocol_RequestMessage_requests_get(reqarr->table));
}

struct partaked_request *
partaked_reqarray_request(struct partaked_reqarray *reqarr, uint32_t index) {
    struct partaked_request *req = partaked_malloc(sizeof(*req));

    req->iobuf = partaked_iobuf_incref(reqarr->iobuf);
    req->table = partake_protocol_Request_vec_at(
        partake_protocol_RequestMessage_requests_get(reqarr->table), index);

    return req;
}

void partaked_request_destroy(struct partaked_request *req) {
    if (req == NULL)
        return;

    partaked_iobuf_decref(req->iobuf);
    partaked_free(req);
}

uint64_t partaked_request_seqno(struct partaked_request *req) {
    return partake_protocol_Request_seqno_get(req->table);
}

partake_protocol_AnyRequest_union_type_t
partaked_request_type(struct partaked_request *req) {
    return partake_protocol_Request_request_type_get(req->table);
}

static inline flatbuffers_generic_t anyrequest(struct partaked_request *req) {
    return partake_protocol_Request_request_get(req->table);
}

bool partaked_request_Echo_skip_response(struct partaked_request *req) {
    return partake_protocol_EchoRequest_skip_response_get(anyrequest(req));
}

const char *partaked_request_Echo_text(struct partaked_request *req) {
    return partake_protocol_EchoRequest_text_get(anyrequest(req));
}

uint32_t partaked_request_Hello_pid(struct partaked_request *req) {
    return partake_protocol_HelloRequest_pid_get(anyrequest(req));
}

const char *partaked_request_Hello_name(struct partaked_request *req) {
    return partake_protocol_HelloRequest_name_get(anyrequest(req));
}

uint32_t partaked_request_GetSegment_segment(struct partaked_request *req) {
    return partake_protocol_GetSegmentRequest_segment_get(anyrequest(req));
}

uint64_t partaked_request_Alloc_size(struct partaked_request *req) {
    return partake_protocol_AllocRequest_size_get(anyrequest(req));
}

bool partaked_request_Alloc_clear(struct partaked_request *req) {
    return partake_protocol_AllocRequest_clear_get(anyrequest(req));
}

uint8_t partaked_request_Alloc_policy(struct partaked_request *req) {
    return partake_protocol_AllocRequest_policy_get(anyrequest(req));
}

uint64_t partaked_request_Open_token(struct partaked_request *req) {
    return partake_protocol_OpenRequest_token_get(anyrequest(req));
}

bool partaked_request_Open_wait(struct partaked_request *req) {
    return partake_protocol_OpenRequest_wait_get(anyrequest(req));
}

uint8_t partaked_request_Open_policy(struct partaked_request *req) {
    return partake_protocol_OpenRequest_policy_get(anyrequest(req));
}

uint64_t partaked_request_Close_token(struct partaked_request *req) {
    return partake_protocol_CloseRequest_token_get(anyrequest(req));
}

uint64_t partaked_request_Share_token(struct partaked_request *req) {
    return partake_protocol_ShareRequest_token_get(anyrequest(req));
}

uint64_t partaked_request_Unshare_token(struct partaked_request *req) {
    return partake_protocol_UnshareRequest_token_get(anyrequest(req));
}

bool partaked_request_Unshare_wait(struct partaked_request *req) {
    return partake_protocol_UnshareRequest_wait_get(anyrequest(req));
}

bool partaked_request_Unshare_clear(struct partaked_request *req) {
    return partake_protocol_UnshareRequest_clear_get(anyrequest(req));
}

uint64_t partaked_request_CreateVoucher_token(struct partaked_request *req) {
    return partake_protocol_CreateVoucherRequest_token_get(anyrequest(req));
}

uint64_t partaked_request_DiscardVoucher_token(struct partaked_request *req) {
    return partake_protocol_DiscardVoucherRequest_token_get(anyrequest(req));
}
