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

#include "partake_malloc.h"
#include "partake_response.h"
#include "partake_sender.h"

#include <assert.h>


struct partake_sender {
    struct partake_responsemessage *respmsg;

    // TODO Fields for sending a separate response message if respmsg is NULL
};


struct partake_sender *partake_sender_create(void) {
    struct partake_sender *sender = partake_malloc(sizeof(*sender));
    sender->respmsg = NULL;
    return sender;
}


void partake_sender_destroy(struct partake_sender *sender) {
    if (sender == NULL)
        return;

    assert (sender->respmsg == NULL);
    partake_free(sender);
}


void partake_sender_attach_responsemessage(struct partake_sender *sender,
        struct partake_responsemessage *respmsg) {
    assert (sender->respmsg == NULL);
    sender->respmsg = respmsg;
}


struct partake_responsemessage *partake_sender_detach_responsemessage(
        struct partake_sender *sender) {
    struct partake_responsemessage *respmsg = sender->respmsg;
    sender->respmsg = NULL;
    return respmsg;
}


struct partake_responsemessage *partake_sender_checkout_responsemessage(
        struct partake_sender *sender) {
    if (sender->respmsg != NULL)
        return sender->respmsg;

    return partake_responsemessage_create();
}


void partake_sender_checkin_responsemessage(struct partake_sender *sender,
        struct partake_responsemessage *respmsg) {
    if (respmsg == sender->respmsg)
        return;

    assert (sender->respmsg == NULL);

    size_t size;
    struct partake_iobuf *iobuf = partake_responsemessage_finish(respmsg,
            &size);
    if (iobuf == NULL)
        return;

    // TODO Send iobuf
}
