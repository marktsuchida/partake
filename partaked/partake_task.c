/*
 * Request handling tasks
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "prefix.h"

#include "partake_channel.h"
#include "partake_connection.h"
#include "partake_handle.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_pool.h"
#include "partake_protocol_reader.h"
#include "partake_request.h"
#include "partake_response.h"
#include "partake_sender.h"
#include "partake_task.h"
#include "partake_voucherqueue.h"

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

    int status;
    struct partake_handle *handle = NULL;

    if (size > SIZE_MAX) {
        status = partake_protocol_Status_OUT_OF_MEMORY;
    }
    else {
        uint8_t policy = partake_request_Alloc_policy(req);
        bool clear = partake_request_Alloc_clear(req);
        status = partake_channel_alloc_object(conn->chan, size, clear,
                policy, &handle);
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
    struct partake_object *voucher;
};


static void continue_task_Open(struct partake_handle *handle, void *data) {
    struct task_Open_continuation_data *d = data;

    if (handle == NULL) // Canceled
        goto exit;

    int status = partake_channel_resume_open_object(d->conn->chan, handle,
            d->voucher);
    if (status != 0) {
        partake_channel_release_handle(d->conn->chan, handle);
        handle = NULL;
    }

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(d->sender);

    partake_resparray_append_Open_response(resparr, d->req, status, handle);

    partake_sender_checkin_resparray(d->sender, resparr);

exit:
    partake_sender_decref(d->sender);
    partake_request_destroy(d->req);
    partake_free(d);
}


int partake_task_Open(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_Open_token(req);
    bool wait = partake_request_Open_wait(req);
    uint8_t policy = partake_request_Open_policy(req);

    struct partake_handle *handle = NULL;
    struct partake_object *voucher = NULL;
    int status = partake_channel_open_object(conn->chan, token, policy,
            &handle, &voucher);
    if (wait && status == partake_protocol_Status_OBJECT_BUSY) {
        struct task_Open_continuation_data *data =
            partake_malloc(sizeof(*data));
        data->conn = conn;
        data->req = req;
        data->sender = partake_sender_incref(sender);
        data->voucher = voucher;

        if (voucher) {
            struct partake_pool *pool = partake_channel_get_pool(conn->chan);
            partake_voucherqueue_remove(partake_pool_get_voucherqueue(pool),
                    voucher);
        }

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
    struct task_Unpublish_continuation_data *d = data;

    if (handle == NULL) // Canceled
        goto exit;

    int status = partake_channel_resume_unpublish_object(d->conn->chan, handle,
            d->clear);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(d->sender);

    partake_resparray_append_Unpublish_response(resparr, d->req, status,
            handle != NULL ? handle->object->token : 0);

    partake_sender_checkin_resparray(d->sender, resparr);

    partake_channel_release_handle(d->conn->chan, handle);

exit:
    partake_sender_decref(d->sender);
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
        data->sender = partake_sender_incref(sender);
        data->clear = clear;

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


int partake_task_CreateVoucher(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token target_token = partake_request_CreateVoucher_token(req);

    struct partake_object *voucher;
    int status = partake_channel_create_voucher(conn->chan, target_token,
            &voucher);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_token voucher_token = voucher ? voucher->token : 0;
    partake_resparray_append_CreateVoucher_response(resparr, req, status,
            voucher_token);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
    return 0;
}


int partake_task_DiscardVoucher(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender) {
    partake_token token = partake_request_DiscardVoucher_token(req);

    struct partake_object *target;
    int status = partake_channel_discard_voucher(conn->chan, token, &target);

    struct partake_resparray *resparr =
        partake_sender_checkout_resparray(sender);

    partake_token target_token = target ? target->token : 0;
    partake_resparray_append_DiscardVoucher_response(resparr, req, status,
            target_token);

    partake_sender_checkin_resparray(sender, resparr);

    partake_request_destroy(req);
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
