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

struct partaked_sender {
    size_t refcount;

    // Created lazily; destroyed on each flush.
    struct partaked_resparray *resparr; // Owning

    // When true, send (flush) after each checkin.
    bool autoflush;

    uv_stream_t *client; // Non-owning
};

struct partaked_sender *partaked_sender_create(uv_stream_t *client) {
    struct partaked_sender *sender = partaked_malloc(sizeof(*sender));
    sender->refcount = 1;
    sender->resparr = NULL;
    sender->autoflush = false;
    sender->client = client;
    return sender;
}

struct partaked_sender *
partaked_sender_incref(struct partaked_sender *sender) {
    ++sender->refcount;
    return sender;
}

void partaked_sender_decref(struct partaked_sender *sender) {
    if (--sender->refcount == 0) {
        assert(sender->resparr == NULL);
        partaked_free(sender);
    }
}

static void on_write_finish(uv_write_t *writereq, int status) {
    if (status < 0)
        ZF_LOGE("uv_write_cb: %s", uv_strerror(status));

    struct partaked_iobuf *iobuf = writereq->data;
    partaked_iobuf_decref(iobuf);
    partaked_free(writereq);
}

void partaked_sender_flush(struct partaked_sender *sender) {
    size_t msgsize;
    struct partaked_iobuf *iobuf =
        partaked_resparray_finish(sender->resparr, &msgsize);
    sender->resparr = NULL;

    if (iobuf != NULL) {
        iobuf->uvbuf.base = iobuf->buffer;
        iobuf->uvbuf.len = msgsize;

        uv_write_t *writereq = partaked_malloc(sizeof(*writereq));
        writereq->data = iobuf;

        int err = uv_write(writereq, sender->client, &iobuf->uvbuf, 1,
                           on_write_finish);
        if (err != 0)
            ZF_LOGE("uv write: %s", uv_strerror(err));
    }
}

void partaked_sender_set_autoflush(struct partaked_sender *sender,
                                   bool autoflush) {
    sender->autoflush = autoflush;
}

struct partaked_resparray *
partaked_sender_checkout_resparray(struct partaked_sender *sender) {
    if (sender->resparr == NULL)
        sender->resparr = partaked_resparray_create();

    return sender->resparr;
}

void partaked_sender_checkin_resparray(struct partaked_sender *sender,
                                       struct partaked_resparray *resparr) {
    assert(sender->resparr != NULL);
    assert(resparr == sender->resparr);

    if (sender->autoflush)
        partaked_sender_flush(sender);
}
