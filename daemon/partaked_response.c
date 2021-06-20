/*
 * Partaked responses
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_handle.h"
#include "partaked_iobuf.h"
#include "partaked_malloc.h"
#include "partaked_object.h"
#include "partaked_request.h"
#include "partaked_response.h"
#include "partaked_segment.h"
#include "partaked_token.h"

#include "partake_protocol_builder.h"

#include <string.h>


#define MESSAGE_ALIGNMENT 8


struct partaked_resparray {
    flatcc_builder_t builder;
    uint32_t count;
};


struct partaked_resparray *partaked_resparray_create(void) {
    struct partaked_resparray *resparr = partaked_malloc(sizeof(*resparr));
    resparr->count = 0;

    flatcc_builder_t *b = &resparr->builder;
    flatcc_builder_init(b);

    partake_protocol_ResponseMessage_start_as_root_with_size(b);
    partake_protocol_ResponseMessage_responses_start(b);

    return resparr;
}


struct partaked_iobuf *partaked_resparray_finish(
        struct partaked_resparray *resparr, size_t *size) {
    flatcc_builder_t *b = &resparr->builder;

    partake_protocol_ResponseMessage_responses_end(b);
    partake_protocol_ResponseMessage_end_as_root(b);

    struct partaked_iobuf *iobuf = NULL;
    *size = 0;
    if (resparr->count > 0) {
        size_t fbsize = flatcc_builder_get_buffer_size(b);
        *size = (fbsize + MESSAGE_ALIGNMENT - 1) & ~(MESSAGE_ALIGNMENT - 1);

        iobuf = partaked_iobuf_create(PARTAKED_IOBUF_STD_SIZE);
        void *buf = flatcc_builder_copy_buffer(b, iobuf->buffer,
                PARTAKED_IOBUF_STD_SIZE);
        assert (buf != NULL);

        size_t padding = *size - fbsize;
        memset((char *)iobuf->buffer + fbsize, 0, padding);
    }

    flatcc_builder_clear(b);
    partaked_free(resparr);

    return iobuf;
}


static inline void start_response(struct partaked_resparray *resparr,
        struct partaked_request *req, int status) {
    ++resparr->count;

    flatcc_builder_t *b = &resparr->builder;
    partake_protocol_ResponseMessage_responses_push_start(b);

    partake_protocol_Response_seqno_add(b, partaked_request_seqno(req));
    partake_protocol_Response_status_add(b, status);
}


static inline void finish_response(struct partaked_resparray *resparr) {
    flatcc_builder_t *b = &resparr->builder;
    partake_protocol_ResponseMessage_responses_push_end(b);
}


void partaked_resparray_append_Hello_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status, uint32_t conn_no) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_HelloResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_HelloResponse_conn_no_add(b, conn_no);
    }

    partake_protocol_Response_response_HelloResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_GetSegment_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status, struct partaked_segment *segment) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_GetSegmentResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_GetSegmentResponse_segment_start(b);
        partake_protocol_SegmentSpec_size_add(b,
                partaked_segment_size(segment));
        partaked_segment_add_mapping_spec(segment, b);
        partake_protocol_GetSegmentResponse_segment_end(b);
    }

    partake_protocol_Response_response_GetSegmentResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_Alloc_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status, struct partaked_handle *handle) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_AllocResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_AllocResponse_object_create(b, handle->object->token,
                0, handle->object->offset, handle->object->size);
    }

    partake_protocol_Response_response_AllocResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_Realloc_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status, struct partaked_handle *handle) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_ReallocResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_ReallocResponse_object_create(b,
                handle->object->token, 0, handle->object->offset,
                handle->object->size);
    }

    partake_protocol_Response_response_ReallocResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_Open_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status, struct partaked_handle *handle) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_OpenResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_OpenResponse_object_create(b, handle->object->token,
                0, handle->object->offset, handle->object->size);
    }

    partake_protocol_Response_response_OpenResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_Close_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_CloseResponse_start(b);
    partake_protocol_Response_response_CloseResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_Publish_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_PublishResponse_start(b);
    partake_protocol_Response_response_PublishResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_Unpublish_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status, partaked_token token) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_UnpublishResponse_start(b);

    if (status == partake_protocol_Status_OK)
        partake_protocol_UnpublishResponse_token_add(b, token);

    partake_protocol_Response_response_UnpublishResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_CreateVoucher_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status, partaked_token token) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_CreateVoucherResponse_start(b);

    if (status == partake_protocol_Status_OK)
        partake_protocol_CreateVoucherResponse_token_add(b, token);

    partake_protocol_Response_response_CreateVoucherResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_DiscardVoucher_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status, partaked_token token) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_DiscardVoucherResponse_start(b);

    if (status == partake_protocol_Status_OK)
        partake_protocol_DiscardVoucherResponse_token_add(b, token);

    partake_protocol_Response_response_DiscardVoucherResponse_end(b);
    finish_response(resparr);
}


void partaked_resparray_append_empty_response(
        struct partaked_resparray *resparr, struct partaked_request *req,
        int status) {
    start_response(resparr, req, status);
    finish_response(resparr);
}
