/*
 * Partake request accessor functions
 *
 *
 * Copyright (C) 2020, The Board of Regents of the University of Wisconsin
 * System
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "prefix.h"

#include "partake_malloc.h"
#include "partake_protocol_verifier.h"
#include "partake_request.h"

#include <zf_log.h>

#include <stdlib.h>


#define MESSAGE_FRAME_ALIGNMENT 8


struct partake_reqarray {
    struct partake_iobuf *iobuf; // Owning, reference-counted
    size_t offset; // Excluding 32-bit size prefix

    partake_protocol_RequestMessage_table_t table;
};


struct partake_request {
    struct partake_iobuf *iobuf; // Owning, reference-counted

    partake_protocol_Request_table_t table;
};


bool partake_requestframe_scan(struct partake_iobuf *iobuf, size_t start,
        size_t size, size_t *frame_size, bool *frame_complete) {
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


struct partake_iobuf *partake_requestframe_maybe_move(
        struct partake_iobuf *iobuf, size_t *start, size_t size) {
    // For now we just always copy unless already at start. TODO Avoid moving
    // when size is large-ish but there is still plenty of room left in iobuf.
    if (*start > 0) {
        struct partake_iobuf *newbuf =
            partake_iobuf_create(PARTAKE_IOBUF_STD_SIZE);
        memcpy(newbuf->buffer, (char *)iobuf->buffer + *start, size);
        partake_iobuf_release(iobuf);
        *start = 0;
        return newbuf;
    }
    return iobuf;
}


struct partake_reqarray *partake_reqarray_create(struct partake_iobuf *iobuf,
        size_t offset) {
    size_t size;
    void *msg = flatbuffers_read_size_prefix((char *)iobuf->buffer + offset,
            &size);
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

    struct partake_reqarray *reqarr = partake_malloc(sizeof(*reqarr));
    partake_iobuf_retain(iobuf);
    reqarr->iobuf = iobuf;
    reqarr->offset = offset;
    reqarr->table = partake_protocol_RequestMessage_as_root(msg);

    return reqarr;
}


void partake_reqarray_destroy(struct partake_reqarray *reqarr) {
    if (reqarr == NULL)
        return;

    partake_iobuf_release(reqarr->iobuf);
    partake_free(reqarr);
}


uint32_t partake_reqarray_count(struct partake_reqarray *reqarr) {
    return partake_protocol_Request_vec_len(
            partake_protocol_RequestMessage_requests_get(reqarr->table));
}


struct partake_request *partake_reqarray_request(
        struct partake_reqarray *reqarr, uint32_t index) {
    struct partake_request *req = partake_malloc(sizeof(*req));

    partake_iobuf_retain(reqarr->iobuf);
    req->iobuf = reqarr->iobuf;
    req->table = partake_protocol_Request_vec_at(
            partake_protocol_RequestMessage_requests_get(reqarr->table),
            index);

    return req;
}


void partake_request_destroy(struct partake_request *req) {
    if (req == NULL)
        return;

    partake_iobuf_release(req->iobuf);
    partake_free(req);
}


uint64_t partake_request_seqno(struct partake_request *req) {
    return partake_protocol_Request_seqno_get(req->table);
}


partake_protocol_AnyRequest_union_type_t partake_request_type(
        struct partake_request *req) {
    return partake_protocol_Request_request_type_get(req->table);
}


static inline flatbuffers_generic_t anyrequest(struct partake_request *req) {
    return partake_protocol_Request_request_get(req->table);
}


uint32_t partake_request_GetSegment_segment(struct partake_request *req) {
    return partake_protocol_GetSegmentRequest_segment_get(anyrequest(req));
}


uint64_t partake_request_Alloc_size(struct partake_request *req) {
    return partake_protocol_AllocRequest_size_get(anyrequest(req));
}


bool partake_request_Alloc_clear(struct partake_request *req) {
    return partake_protocol_AllocRequest_clear_get(anyrequest(req));
}


bool partake_request_Alloc_share_mutable(struct partake_request *req) {
    return partake_protocol_AllocRequest_share_mutable_get(anyrequest(req));
}


uint64_t partake_request_Realloc_token(struct partake_request *req) {
    return partake_protocol_ReallocRequest_token_get(anyrequest(req));
}


uint64_t partake_request_Realloc_size(struct partake_request *req) {
    return partake_protocol_ReallocRequest_size_get(anyrequest(req));
}


uint64_t partake_request_Open_token(struct partake_request *req) {
    return partake_protocol_OpenRequest_token_get(anyrequest(req));
}


bool partake_request_Open_wait(struct partake_request *req) {
    return partake_protocol_OpenRequest_wait_get(anyrequest(req));
}


bool partake_request_Open_share_mutable(struct partake_request *req) {
    return partake_protocol_OpenRequest_share_mutable_get(anyrequest(req));
}


uint64_t partake_request_Close_token(struct partake_request *req) {
    return partake_protocol_CloseRequest_token_get(anyrequest(req));
}


uint64_t partake_request_Publish_token(struct partake_request *req) {
    return partake_protocol_PublishRequest_token_get(anyrequest(req));
}


uint64_t partake_request_Unpublish_token(struct partake_request *req) {
    return partake_protocol_UnpublishRequest_token_get(anyrequest(req));
}


bool partake_request_Unpublish_wait(struct partake_request *req) {
    return partake_protocol_UnpublishRequest_wait_get(anyrequest(req));
}


bool partake_request_Unpublish_clear(struct partake_request *req) {
    return partake_protocol_UnpublishRequest_clear_get(anyrequest(req));
}
