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
#include "partake_connection.h"
#include "partake_handle.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_protocol_reader.h"
#include "partake_request.h"
#include "partake_response.h"
#include "partake_sender.h"
#include "partake_task.h"

#include <stdbool.h>
#include <stdint.h>


int partake_task_handle(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_protocol_AnyRequest_union_type_t type = partake_request_type(req);

    int (*func)(struct partake_connection *, struct partake_request *,
            struct partake_sender *);
    switch (type) {
        case partake_protocol_AnyRequest_HelloRequest:
            func = partake_task_Hello;
            break;

        case partake_protocol_AnyRequest_QuitRequest:
            func = partake_task_Quit;
            break;

        case partake_protocol_AnyRequest_GetSegmentRequest:
            func = partake_task_GetSegment;
            break;

        case partake_protocol_AnyRequest_AllocRequest:
            func = partake_task_Alloc;
            break;

        case partake_protocol_AnyRequest_ReallocRequest:
            func = partake_task_Realloc;
            break;

        case partake_protocol_AnyRequest_OpenRequest:
            func = partake_task_Open;
            break;

        case partake_protocol_AnyRequest_CloseRequest:
            func = partake_task_Close;
            break;

        case partake_protocol_AnyRequest_PublishRequest:
            func = partake_task_Publish;
            break;

        case partake_protocol_AnyRequest_UnpublishRequest:
            func = partake_task_Unpublish;
            break;

        default:
            func = partake_task_Unknown;
            break;
    }

    if (!conn->has_said_hello &&
            type != partake_protocol_AnyRequest_HelloRequest)
        return -1;

    return func(conn, req, sender);
}


int partake_task_Hello(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    uint32_t conn_no = 0;
    int status = 0;
    if (conn->has_said_hello) {
        status = partake_protocol_Status_INVALID_REQUEST;
    }
    else {
        conn->has_said_hello = true;
        conn->pid = partake_request_Hello_pid(req);

        const char *name = partake_request_Hello_name(req);
        if (name != NULL && name[0] != '\0' && conn->name == NULL) {
            // Truncate to 1023 bytes (not strictly correct for UTF-8)
            size_t namelen = strnlen(name, 1024);
            conn->name = partake_malloc(namelen);
            snprintf(conn->name, namelen, "%s", name);
        }

        conn_no = conn->conn_no;
    }

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Hello_response(resparr, req, status, conn_no);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
    return 0;
}


int partake_task_Quit(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_request_destroy(req);
    return -1;
}


int partake_task_GetSegment(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    uint32_t segno = partake_request_GetSegment_segment(req);

    struct partake_segment *segment = NULL;
    int status = partake_channel_get_segment(conn->chan, segno, &segment);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_GetSegment_response(resparr, req, status,
            segment);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
    return 0;
}


int partake_task_Alloc(struct partake_connection *conn,
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
        status = partake_channel_alloc_object(conn->chan, size, clear,
                share_mutable, &handle);
    }

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Alloc_response(resparr, req, status,
            handle);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
    return 0;
}


int partake_task_Realloc(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Realloc_token(req);
    uint64_t size = partake_request_Realloc_size(req);

    int status;
    struct partake_handle *handle = NULL;

    if (size > SIZE_MAX) {
        status = partake_protocol_Status_OUT_OF_MEMORY;
    }
    else {
        status = partake_channel_realloc_object(conn->chan, token, size,
                &handle);
    }

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Realloc_response(resparr, req, status,
            handle);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
    return 0;
}


struct task_Open_continuation_data {
    struct partake_connection *conn;
    struct partake_request *req;
    struct partake_sender *sender;
};


static void continue_task_Open(struct partake_handle *handle, void *data) {
    if (handle == NULL) // Canceled
        goto exit;

    struct task_Open_continuation_data *d = data;

    int status = partake_channel_resume_open_object(d->conn->chan, handle);
    if (status != 0) {
        partake_channel_release_handle(d->conn->chan, handle);
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


int partake_task_Open(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Open_token(req);
    bool wait = partake_request_Open_wait(req);
    bool share_mutable = partake_request_Open_share_mutable(req);

    struct partake_handle *handle = NULL;
    int status = partake_channel_open_object(conn->chan, token, share_mutable,
            &handle);
    if (wait && status == partake_protocol_Status_OBJECT_BUSY) {
        struct task_Open_continuation_data *data =
            partake_malloc(sizeof(*data));
        data->conn = conn;
        data->req = req;
        data->sender = sender;
        partake_sender_retain(sender);

        partake_handle_register_continue_on_publish(handle, req,
                continue_task_Open, data);
    }
    else {
        if (status != 0 && handle != NULL) {
            partake_channel_release_handle(conn->chan, handle);
            handle = NULL;
        }

        struct partake_resparray *resparr =
            partake_sender_checkout_resparray(sender);

        partake_resparray_append_Open_response(resparr, req, status, handle);

        partake_sender_checkin_resparray(sender, resparr);

        partake_request_destroy(req);
    }

    return 0;
}


int partake_task_Close(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Close_token(req);

    int status = partake_channel_close_object(conn->chan, token);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Close_response(resparr, req, status);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
    return 0;
}


int partake_task_Publish(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Publish_token(req);

    int status = partake_channel_publish_object(conn->chan, token);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_Publish_response(resparr, req, status);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
    return 0;
}


struct task_Unpublish_continuation_data {
    struct partake_connection *conn;
    struct partake_request *req;
    struct partake_sender *sender;
    bool clear;
};


static void continue_task_Unpublish(struct partake_handle *handle,
        void *data) {
    if (handle == NULL) // Canceled
        goto exit;

    struct task_Unpublish_continuation_data *d = data;

    int status = partake_channel_resume_unpublish_object(d->conn->chan, handle,
            d->clear);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(d->sender);

    partake_resparray_append_Unpublish_response(resparr, d->req, status,
            handle != NULL ? handle->object->token : 0);

    partake_sender_checkin_resparray(d->sender, resparr);

    partake_channel_release_handle(d->conn->chan, handle);

exit:
    partake_sender_release(d->sender);
    partake_request_destroy(d->req);
    partake_free(d);
}


int partake_task_Unpublish(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Unpublish_token(req);
    bool clear = partake_request_Unpublish_clear(req);
    bool wait = partake_request_Unpublish_wait(req);

    struct partake_handle *handle = NULL;
    int status = partake_channel_unpublish_object(conn->chan, token, clear,
            &handle);
    if (wait && status == partake_protocol_Status_OBJECT_BUSY) {
        ++handle->refcount;

        struct task_Unpublish_continuation_data *data =
            partake_malloc(sizeof(*data));
        data->conn = conn;
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

    return 0;
}


int partake_task_Unknown(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_resparray_append_empty_response(resparr, req,
            partake_protocol_Status_INVALID_REQUEST);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
    return 0;
}
