/*
 * Partaked responses
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "prefix.h"

#include "partake_handle.h"
#include "partake_iobuf.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_protocol_builder.h"
#include "partake_request.h"
#include "partake_response.h"
#include "partake_segment.h"
#include "partake_token.h"

#include <string.h>


#define MESSAGE_ALIGNMENT 8


struct partake_resparray {
    flatcc_builder_t builder;
    uint32_t count;
};


struct partake_resparray *partake_resparray_create(void) {
    struct partake_resparray *resparr = partake_malloc(sizeof(*resparr));
    resparr->count = 0;

    flatcc_builder_t *b = &resparr->builder;
    flatcc_builder_init(b);

    partake_protocol_ResponseMessage_start_as_root_with_size(b);
    partake_protocol_ResponseMessage_responses_start(b);

    return resparr;
}


struct partake_iobuf *partake_resparray_finish(
        struct partake_resparray *resparr, size_t *size) {
    flatcc_builder_t *b = &resparr->builder;

    partake_protocol_ResponseMessage_responses_end(b);
    partake_protocol_ResponseMessage_end_as_root(b);

    struct partake_iobuf *iobuf = NULL;
    *size = 0;
    if (resparr->count > 0) {
        size_t fbsize = flatcc_builder_get_buffer_size(b);
        *size = (fbsize + MESSAGE_ALIGNMENT - 1) & ~(MESSAGE_ALIGNMENT - 1);

        iobuf = partake_iobuf_create(PARTAKE_IOBUF_STD_SIZE);
        void *buf = flatcc_builder_copy_buffer(b, iobuf->buffer,
                PARTAKE_IOBUF_STD_SIZE);
        assert (buf != NULL);

        size_t padding = *size - fbsize;
        memset((char *)iobuf->buffer + fbsize, 0, padding);
    }

    flatcc_builder_clear(b);
    partake_free(resparr);

    return iobuf;
}


static inline void start_response(struct partake_resparray *resparr,
        struct partake_request *req, int status) {
    ++resparr->count;

    flatcc_builder_t *b = &resparr->builder;
    partake_protocol_ResponseMessage_responses_push_start(b);

    partake_protocol_Response_seqno_add(b, partake_request_seqno(req));
    partake_protocol_Response_status_add(b, status);
}


static inline void finish_response(struct partake_resparray *resparr) {
    flatcc_builder_t *b = &resparr->builder;
    partake_protocol_ResponseMessage_responses_push_end(b);
}


void partake_resparray_append_Hello_response(
        struct partake_resparray *resparr, struct partake_request *req,
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


void partake_resparray_append_GetSegment_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, struct partake_segment *segment) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_GetSegmentResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_GetSegmentResponse_segment_start(b);
        partake_protocol_SegmentSpec_size_add(b,
                partake_segment_size(segment));
        partake_segment_add_mapping_spec(segment, b);
        partake_protocol_GetSegmentResponse_segment_end(b);
    }

    partake_protocol_Response_response_GetSegmentResponse_end(b);
    finish_response(resparr);
}


void partake_resparray_append_Alloc_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, struct partake_handle *handle) {
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


void partake_resparray_append_Realloc_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, struct partake_handle *handle) {
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


void partake_resparray_append_Open_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, struct partake_handle *handle) {
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


void partake_resparray_append_Close_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_CloseResponse_start(b);
    partake_protocol_Response_response_CloseResponse_end(b);
    finish_response(resparr);
}


void partake_resparray_append_Publish_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_PublishResponse_start(b);
    partake_protocol_Response_response_PublishResponse_end(b);
    finish_response(resparr);
}


void partake_resparray_append_Unpublish_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, partake_token token) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_UnpublishResponse_start(b);

    if (status == partake_protocol_Status_OK)
        partake_protocol_UnpublishResponse_token_add(b, token);

    partake_protocol_Response_response_UnpublishResponse_end(b);
    finish_response(resparr);
}


void partake_resparray_append_CreateVoucher_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, partake_token token) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_CreateVoucherResponse_start(b);

    if (status == partake_protocol_Status_OK)
        partake_protocol_CreateVoucherResponse_token_add(b, token);

    partake_protocol_Response_response_CreateVoucherResponse_end(b);
    finish_response(resparr);
}


void partake_resparray_append_DiscardVoucher_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, partake_token token) {
    flatcc_builder_t *b = &resparr->builder;

    start_response(resparr, req, status);
    partake_protocol_Response_response_DiscardVoucherResponse_start(b);

    if (status == partake_protocol_Status_OK)
        partake_protocol_DiscardVoucherResponse_token_add(b, token);

    partake_protocol_Response_response_DiscardVoucherResponse_end(b);
    finish_response(resparr);
}


void partake_resparray_append_empty_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status) {
    start_response(resparr, req, status);
    finish_response(resparr);
}
