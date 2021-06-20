/*
 * Request handling tasks
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_channel.h"
#include "partaked_connection.h"
#include "partaked_handle.h"
#include "partaked_malloc.h"
#include "partaked_object.h"
#include "partaked_pool.h"
#include "partaked_request.h"
#include "partaked_response.h"
#include "partaked_sender.h"
#include "partaked_task.h"
#include "partaked_voucherqueue.h"

#include "partake_protocol_reader.h"

#include <stdbool.h>
#include <stdint.h>


int partaked_task_handle(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partake_protocol_AnyRequest_union_type_t type = partaked_request_type(req);

    int (*func)(struct partaked_connection *, struct partaked_request *,
            struct partaked_sender *);
    switch (type) {
        case partake_protocol_AnyRequest_HelloRequest:
            func = partaked_task_Hello;
            break;

        case partake_protocol_AnyRequest_QuitRequest:
            func = partaked_task_Quit;
            break;

        case partake_protocol_AnyRequest_GetSegmentRequest:
            func = partaked_task_GetSegment;
            break;

        case partake_protocol_AnyRequest_AllocRequest:
            func = partaked_task_Alloc;
            break;

        case partake_protocol_AnyRequest_ReallocRequest:
            func = partaked_task_Realloc;
            break;

        case partake_protocol_AnyRequest_OpenRequest:
            func = partaked_task_Open;
            break;

        case partake_protocol_AnyRequest_CloseRequest:
            func = partaked_task_Close;
            break;

        case partake_protocol_AnyRequest_PublishRequest:
            func = partaked_task_Publish;
            break;

        case partake_protocol_AnyRequest_UnpublishRequest:
            func = partaked_task_Unpublish;
            break;

        default:
            func = partaked_task_Unknown;
            break;
    }

    if (!conn->has_said_hello &&
            type != partake_protocol_AnyRequest_HelloRequest)
        return -1;

    return func(conn, req, sender);
}


int partaked_task_Hello(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    uint32_t conn_no = 0;
    int status = 0;
    if (conn->has_said_hello) {
        status = partake_protocol_Status_INVALID_REQUEST;
    }
    else {
        conn->has_said_hello = true;
        conn->pid = partaked_request_Hello_pid(req);

        const char *name = partaked_request_Hello_name(req);
        if (name != NULL && name[0] != '\0' && conn->name == NULL) {
            // Truncate to 1023 bytes (not strictly correct for UTF-8)
            size_t namelen = strnlen(name, 1024);
            conn->name = partaked_malloc(namelen);
            snprintf(conn->name, namelen, "%s", name);
        }

        conn_no = conn->conn_no;
    }

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_resparray_append_Hello_response(resparr, req, status, conn_no);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}


int partaked_task_Quit(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partaked_request_destroy(req);
    return -1;
}


int partaked_task_GetSegment(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    uint32_t segno = partaked_request_GetSegment_segment(req);

    struct partaked_segment *segment = NULL;
    int status = partaked_channel_get_segment(conn->chan, segno, &segment);

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_resparray_append_GetSegment_response(resparr, req, status,
            segment);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}


int partaked_task_Alloc(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    uint64_t size = partaked_request_Alloc_size(req);

    int status;
    struct partaked_handle *handle = NULL;

    if (size > SIZE_MAX) {
        status = partake_protocol_Status_OUT_OF_MEMORY;
    }
    else {
        uint8_t policy = partaked_request_Alloc_policy(req);
        bool clear = partaked_request_Alloc_clear(req);
        status = partaked_channel_alloc_object(conn->chan, size, clear,
                policy, &handle);
    }

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_resparray_append_Alloc_response(resparr, req, status,
            handle);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}


int partaked_task_Realloc(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partaked_token token = partaked_request_Realloc_token(req);
    uint64_t size = partaked_request_Realloc_size(req);

    int status;
    struct partaked_handle *handle = NULL;

    if (size > SIZE_MAX) {
        status = partake_protocol_Status_OUT_OF_MEMORY;
    }
    else {
        status = partaked_channel_realloc_object(conn->chan, token, size,
                &handle);
    }

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_resparray_append_Realloc_response(resparr, req, status,
            handle);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}


struct task_Open_continuation_data {
    struct partaked_connection *conn;
    struct partaked_request *req;
    struct partaked_sender *sender;
    struct partaked_object *voucher;
};


static void continue_task_Open(struct partaked_handle *handle, void *data) {
    struct task_Open_continuation_data *d = data;

    if (handle == NULL) // Canceled
        goto exit;

    int status = partaked_channel_resume_open_object(d->conn->chan, handle,
            d->voucher);
    if (status != 0) {
        partaked_channel_release_handle(d->conn->chan, handle);
        handle = NULL;
    }

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(d->sender);

    partaked_resparray_append_Open_response(resparr, d->req, status, handle);

    partaked_sender_checkin_resparray(d->sender, resparr);

exit:
    partaked_sender_decref(d->sender);
    partaked_request_destroy(d->req);
    partaked_free(d);
}


int partaked_task_Open(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partaked_token token = partaked_request_Open_token(req);
    bool wait = partaked_request_Open_wait(req);
    uint8_t policy = partaked_request_Open_policy(req);

    struct partaked_handle *handle = NULL;
    struct partaked_object *voucher = NULL;
    int status = partaked_channel_open_object(conn->chan, token, policy,
            &handle, &voucher);
    if (wait && status == partake_protocol_Status_OBJECT_BUSY) {
        struct task_Open_continuation_data *data =
            partaked_malloc(sizeof(*data));
        data->conn = conn;
        data->req = req;
        data->sender = partaked_sender_incref(sender);
        data->voucher = voucher;

        if (voucher) {
            struct partaked_pool *pool = partaked_channel_get_pool(conn->chan);
            partaked_voucherqueue_remove(partaked_pool_get_voucherqueue(pool),
                    voucher);
        }

        partaked_handle_register_continue_on_publish(handle, req,
                continue_task_Open, data);
    }
    else {
        if (status != 0 && handle != NULL) {
            partaked_channel_release_handle(conn->chan, handle);
            handle = NULL;
        }

        struct partaked_resparray *resparr =
            partaked_sender_checkout_resparray(sender);

        partaked_resparray_append_Open_response(resparr, req, status, handle);

        partaked_sender_checkin_resparray(sender, resparr);

        partaked_request_destroy(req);
    }

    return 0;
}


int partaked_task_Close(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partaked_token token = partaked_request_Close_token(req);

    int status = partaked_channel_close_object(conn->chan, token);

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_resparray_append_Close_response(resparr, req, status);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}


int partaked_task_Publish(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partaked_token token = partaked_request_Publish_token(req);

    int status = partaked_channel_publish_object(conn->chan, token);

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_resparray_append_Publish_response(resparr, req, status);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}


struct task_Unpublish_continuation_data {
    struct partaked_connection *conn;
    struct partaked_request *req;
    struct partaked_sender *sender;
    bool clear;
};


static void continue_task_Unpublish(struct partaked_handle *handle,
        void *data) {
    struct task_Unpublish_continuation_data *d = data;

    if (handle == NULL) // Canceled
        goto exit;

    int status = partaked_channel_resume_unpublish_object(d->conn->chan, handle,
            d->clear);

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(d->sender);

    partaked_resparray_append_Unpublish_response(resparr, d->req, status,
            handle != NULL ? handle->object->token : 0);

    partaked_sender_checkin_resparray(d->sender, resparr);

    partaked_channel_release_handle(d->conn->chan, handle);

exit:
    partaked_sender_decref(d->sender);
    partaked_request_destroy(d->req);
    partaked_free(d);
}


int partaked_task_Unpublish(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partaked_token token = partaked_request_Unpublish_token(req);
    bool clear = partaked_request_Unpublish_clear(req);
    bool wait = partaked_request_Unpublish_wait(req);

    struct partaked_handle *handle = NULL;
    int status = partaked_channel_unpublish_object(conn->chan, token, clear,
            &handle);
    if (wait && status == partake_protocol_Status_OBJECT_BUSY) {
        ++handle->refcount;

        struct task_Unpublish_continuation_data *data =
            partaked_malloc(sizeof(*data));
        data->conn = conn;
        data->req = req;
        data->sender = partaked_sender_incref(sender);
        data->clear = clear;

        partaked_handle_register_continue_on_sole_ownership(handle, req,
                continue_task_Unpublish, data);
    }
    else {
        struct partaked_resparray *resparr =
            partaked_sender_checkout_resparray(sender);

        partaked_resparray_append_Unpublish_response(resparr, req, status,
                handle != NULL ? handle->object->token : 0);

        partaked_sender_checkin_resparray(sender, resparr);

        partaked_request_destroy(req);
    }

    return 0;
}


int partaked_task_CreateVoucher(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partaked_token target_token = partaked_request_CreateVoucher_token(req);

    struct partaked_object *voucher;
    int status = partaked_channel_create_voucher(conn->chan, target_token,
            &voucher);

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_token voucher_token = voucher ? voucher->token : 0;
    partaked_resparray_append_CreateVoucher_response(resparr, req, status,
            voucher_token);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}


int partaked_task_DiscardVoucher(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    partaked_token token = partaked_request_DiscardVoucher_token(req);

    struct partaked_object *target;
    int status = partaked_channel_discard_voucher(conn->chan, token, &target);

    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_token target_token = target ? target->token : 0;
    partaked_resparray_append_DiscardVoucher_response(resparr, req, status,
            target_token);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}


int partaked_task_Unknown(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender) {
    struct partaked_resparray *resparr =
        partaked_sender_checkout_resparray(sender);

    partaked_resparray_append_empty_response(resparr, req,
            partake_protocol_Status_INVALID_REQUEST);

    partaked_sender_checkin_resparray(sender, resparr);

    partaked_request_destroy(req);
    return 0;
}
