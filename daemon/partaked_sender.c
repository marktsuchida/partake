/*
 * Collect and send partaked responses
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_iobuf.h"
#include "partaked_malloc.h"
#include "partaked_response.h"
#include "partaked_sender.h"

#include <uv.h>
#include <zf_log.h>

#include <assert.h>


struct partake_sender {
    size_t refcount;

    // Created lazily; destroyed on each flush.
    struct partake_resparray *resparr; // Owning

    // When true, send (flush) after each checkin.
    bool autoflush;

    uv_stream_t *client; // Non-owning
};


struct partake_sender *partake_sender_create(uv_stream_t *client) {
    struct partake_sender *sender = partake_malloc(sizeof(*sender));
    sender->refcount = 1;
    sender->resparr = NULL;
    sender->autoflush = false;
    sender->client = client;
    return sender;
}


struct partake_sender *partake_sender_incref(struct partake_sender *sender) {
    ++sender->refcount;
    return sender;
}


void partake_sender_decref(struct partake_sender *sender) {
    if (--sender->refcount == 0) {
        assert (sender->resparr == NULL);
        partake_free(sender);
    }
}


static void on_write_finish(uv_write_t *writereq, int status) {
    if (status < 0)
        ZF_LOGE("uv_write_cb: %s", uv_strerror(status));

    struct partake_iobuf *iobuf = writereq->data;
    partake_iobuf_decref(iobuf);
    partake_free(writereq);
}


void partake_sender_flush(struct partake_sender *sender) {
    size_t msgsize;
    struct partake_iobuf *iobuf =
        partake_resparray_finish(sender->resparr, &msgsize);
    sender->resparr = NULL;

    if (iobuf != NULL) {
        iobuf->uvbuf.base = iobuf->buffer;
        iobuf->uvbuf.len = msgsize;

        uv_write_t *writereq = partake_malloc(sizeof(*writereq));
        writereq->data = iobuf;

        int err = uv_write(writereq, sender->client, &iobuf->uvbuf, 1,
                on_write_finish);
        if (err != 0)
            ZF_LOGE("uv write: %s", uv_strerror(err));
    }
}


void partake_sender_set_autoflush(struct partake_sender *sender,
        bool autoflush) {
    sender->autoflush = autoflush;
}


struct partake_resparray *partake_sender_checkout_resparray(
        struct partake_sender *sender) {
    if (sender->resparr == NULL)
        sender->resparr = partake_resparray_create();

    return sender->resparr;
}


void partake_sender_checkin_resparray(struct partake_sender *sender,
        struct partake_resparray *resparr) {
    assert (sender->resparr != NULL);
    assert (resparr == sender->resparr);

    if (sender->autoflush)
        partake_sender_flush(sender);
}
