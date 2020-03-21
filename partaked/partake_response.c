/*
 * Partaked responses
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

#include "partake_handle.h"
#include "partake_iobuf.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_protocol_builder.h"
#include "partake_request.h"
#include "partake_response.h"
#include "partake_token.h"

#include <string.h>


#define MESSAGE_ALIGNMENT 8


struct partake_responsemessage {
    flatcc_builder_t builder;
    uint32_t count;
};


struct partake_responsemessage *partake_responsemessage_create(void) {
    struct partake_responsemessage *respmsg = partake_malloc(sizeof(*respmsg));
    respmsg->count = 0;

    flatcc_builder_t *b = &respmsg->builder;
    flatcc_builder_init(b);

    partake_protocol_ResponseMessage_start_as_root_with_size(b);
    partake_protocol_ResponseMessage_responses_start(b);

    return respmsg;
}


struct partake_iobuf *partake_responsemessage_finish(
        struct partake_responsemessage *respmsg, size_t *size) {
    flatcc_builder_t *b = &respmsg->builder;

    partake_protocol_ResponseMessage_responses_end(b);
    partake_protocol_ResponseMessage_end_as_root(b);

    struct partake_iobuf *iobuf = NULL;
    *size = 0;
    if (respmsg->count > 0) {
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
    partake_free(respmsg);

    return iobuf;
}


static inline void responsemessage_start_response(flatcc_builder_t *b,
        struct partake_request *req, int status) {
    partake_protocol_ResponseMessage_responses_push_start(b);

    partake_protocol_Response_seqno_add(b, partake_request_seqno(req));
    partake_protocol_Response_status_add(b, status);
}


static inline void responsemessage_finish_response(flatcc_builder_t *b) {
    partake_protocol_ResponseMessage_responses_push_end(b);
}


void partake_responsemessage_append_GetSegment_response(
        struct partake_responsemessage *respmsg, struct partake_request *req,
        int status) {
    flatcc_builder_t *b = &respmsg->builder;

    responsemessage_start_response(b, req, status);
    partake_protocol_Response_response_GetSegmentResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        // TODO
    }

    partake_protocol_Response_response_GetSegmentResponse_end(b);
    responsemessage_finish_response(b);
}


void partake_responsemessage_append_Alloc_response(
        struct partake_responsemessage *respmsg, struct partake_request *req,
        int status, struct partake_handle *handle) {
    flatcc_builder_t *b = &respmsg->builder;

    responsemessage_start_response(b, req, status);
    partake_protocol_Response_response_AllocResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_AllocResponse_object_create(b, handle->object->token,
                0, handle->object->offset, handle->object->size);
    }

    partake_protocol_Response_response_AllocResponse_end(b);
    responsemessage_finish_response(b);
}


void partake_responsemessage_append_Realloc_response(
        struct partake_responsemessage *respmsg, struct partake_request *req,
        int status, struct partake_handle *handle) {
    flatcc_builder_t *b = &respmsg->builder;

    responsemessage_start_response(b, req, status);
    partake_protocol_Response_response_ReallocResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_ReallocResponse_object_create(b,
                handle->object->token, 0, handle->object->offset,
                handle->object->size);
    }

    partake_protocol_Response_response_ReallocResponse_end(b);
    responsemessage_finish_response(b);
}


void partake_responsemessage_append_Open_response(
        struct partake_responsemessage *respmsg, struct partake_request *req,
        int status, struct partake_handle *handle) {
    flatcc_builder_t *b = &respmsg->builder;

    responsemessage_start_response(b, req, status);
    partake_protocol_Response_response_OpenResponse_start(b);

    if (status == partake_protocol_Status_OK) {
        partake_protocol_OpenResponse_object_create(b, handle->object->token,
                0, handle->object->offset, handle->object->size);
    }

    partake_protocol_Response_response_OpenResponse_end(b);
    responsemessage_finish_response(b);
}


void partake_responsemessage_append_Close_response(
        struct partake_responsemessage *respmsg, struct partake_request *req,
        int status) {
    flatcc_builder_t *b = &respmsg->builder;

    responsemessage_start_response(b, req, status);
    partake_protocol_Response_response_CloseResponse_start(b);
    partake_protocol_Response_response_CloseResponse_end(b);
    responsemessage_finish_response(b);
}


void partake_responsemessage_append_Publish_response(
        struct partake_responsemessage *respmsg, struct partake_request *req,
        int status) {
    flatcc_builder_t *b = &respmsg->builder;

    responsemessage_start_response(b, req, status);
    partake_protocol_Response_response_PublishResponse_start(b);
    partake_protocol_Response_response_PublishResponse_end(b);
    responsemessage_finish_response(b);
}


void partake_responsemessage_append_Unpublish_response(
        struct partake_responsemessage *respmsg, struct partake_request *req,
        int status, partake_token token) {
    flatcc_builder_t *b = &respmsg->builder;

    responsemessage_start_response(b, req, status);
    partake_protocol_Response_response_UnpublishResponse_start(b);

    if (status == partake_protocol_Status_OK)
        partake_protocol_UnpublishResponse_token_add(b, token);

    partake_protocol_Response_response_UnpublishResponse_end(b);
    responsemessage_finish_response(b);
}
