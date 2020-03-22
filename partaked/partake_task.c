/*
 * Request handling tasks
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

#include "partake_channel.h"
#include "partake_handle.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_protocol_builder.h"
#include "partake_request.h"
#include "partake_response.h"
#include "partake_sender.h"
#include "partake_task.h"

#include <stdbool.h>
#include <stdint.h>


void partake_task_GetSegment(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    uint32_t segno = partake_request_GetSegment_segment(req);

    struct partake_segment *segment = NULL;
    int status = partake_channel_get_segment(chan, segno, &segment);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_GetSegment_response(resparr, req, status,
            segment);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
}


void partake_task_Alloc(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    uint64_t size = partake_request_Alloc_size(req);
    bool share_mutable = partake_request_Alloc_share_mutable(req);
    bool clear = partake_request_Alloc_clear(req);

    int status;
    struct partake_handle *handle = NULL;

    if (size > SIZE_MAX) {
        status = partake_protocol_Status_OUT_OF_MEMORY;
    }
    else {
        status = partake_channel_alloc_object(chan, size, clear, share_mutable,
                &handle);
    }

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Alloc_response(resparr, req, status,
            handle);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
}


void partake_task_Realloc(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Realloc_token(req);
    uint64_t size = partake_request_Realloc_size(req);

    int status;
    struct partake_handle *handle = NULL;

    if (size > SIZE_MAX) {
        status = partake_protocol_Status_OUT_OF_MEMORY;
    }
    else {
        status = partake_channel_realloc_object(chan, token, size, &handle);
    }

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Realloc_response(resparr, req, status,
            handle);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
}


struct task_Open_continuation_data {
    struct partake_channel *chan;
    struct partake_request *req;
    struct partake_sender *sender;
};


static void continue_task_Open(struct partake_handle *handle, void *data) {
    if (handle == NULL) // Canceled
        goto exit;

    struct task_Open_continuation_data *d = data;

    int status = partake_channel_resume_open_object(d->chan, handle);
    if (status != 0) {
        partake_channel_release_handle(d->chan, handle);
        handle = NULL;
    }

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(d->sender);

    partake_resparray_append_Open_response(resparr, d->req, status, handle);

    partake_sender_checkin_resparray(d->sender, resparr);

exit:
    partake_sender_release(d->sender);
    partake_request_destroy(d->req);
    partake_free(d);
}


void partake_task_Open(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Open_token(req);
    bool wait = partake_request_Open_wait(req);
    bool share_mutable = partake_request_Open_share_mutable(req);

    struct partake_handle *handle = NULL;
    int status = partake_channel_open_object(chan, token, share_mutable,
            &handle);
    if (wait && status == partake_protocol_Status_OBJECT_BUSY) {
        struct task_Open_continuation_data *data =
            partake_malloc(sizeof(*data));
        data->chan = chan;
        data->req = req;
        data->sender = sender;
        partake_sender_retain(sender);

        partake_handle_register_continue_on_publish(handle, req,
                continue_task_Open, data);
    }
    else {
        if (status != 0 && handle != NULL) {
            partake_channel_release_handle(chan, handle);
            handle = NULL;
        }

        struct partake_resparray *resparr =
            partake_sender_checkout_resparray(sender);

        partake_resparray_append_Open_response(resparr, req, status, handle);

        partake_sender_checkin_resparray(sender, resparr);

        partake_request_destroy(req);
    }
}


void partake_task_Close(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Close_token(req);

    int status = partake_channel_close_object(chan, token);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Close_response(resparr, req, status);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
}


void partake_task_Publish(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Publish_token(req);

    int status = partake_channel_publish_object(chan, token);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Publish_response(resparr, req, status);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
}


struct task_Unpublish_continuation_data {
    struct partake_channel *chan;
    struct partake_request *req;
    struct partake_sender *sender;
    bool clear;
};


static void continue_task_Unpublish(struct partake_handle *handle,
        void *data) {
    if (handle == NULL) // Canceled
        goto exit;

    struct task_Unpublish_continuation_data *d = data;

    int status = partake_channel_resume_unpublish_object(d->chan, handle,
            d->clear);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(d->sender);

    partake_resparray_append_Unpublish_response(resparr, d->req, status,
            handle != NULL ? handle->object->token : 0);

    partake_sender_checkin_resparray(d->sender, resparr);

    partake_channel_release_handle(d->chan, handle);

exit:
    partake_sender_release(d->sender);
    partake_request_destroy(d->req);
    partake_free(d);
}


void partake_task_Unpublish(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Unpublish_token(req);
    bool clear = partake_request_Unpublish_clear(req);
    bool wait = partake_request_Unpublish_wait(req);

    struct partake_handle *handle = NULL;
    int status = partake_channel_unpublish_object(chan, token, clear, &handle);
    if (wait && status == partake_protocol_Status_OBJECT_BUSY) {
        ++handle->refcount;

        struct task_Unpublish_continuation_data *data =
            partake_malloc(sizeof(*data));
        data->chan = chan;
        data->req = req;
        data->sender = sender;
        data->clear = clear;
        partake_sender_retain(sender);

        partake_handle_register_continue_on_sole_ownership(handle, req,
                continue_task_Unpublish, data);
    }
    else {
        struct partake_resparray *resparr =
            partake_sender_checkout_resparray(sender);

        partake_resparray_append_Unpublish_response(resparr, req, status,
                handle != NULL ? handle->object->token : 0);

        partake_sender_checkin_resparray(sender, resparr);

        partake_request_destroy(req);
    }
}


void partake_task_Quit(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    // Actual quitting is handled by connection.

    partake_request_destroy(req);
}


void partake_task_Unknown(struct partake_channel *chan,
        struct partake_request *req, struct partake_sender *sender) {
    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_empty_response(resparr, req,
            partake_protocol_Status_INVALID_REQUEST);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
}
