/*
 * Collect and send partaked responses
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

#include "partake_iobuf.h"
#include "partake_malloc.h"
#include "partake_response.h"
#include "partake_sender.h"

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
