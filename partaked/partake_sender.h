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

#pragma once

struct partake_responsemessage;


struct partake_sender *partake_sender_create(void);

void partake_sender_destroy(struct partake_sender *sender);


/*
 * "Attach" and "detach" a response message: the sender adds responses to the
 * attached response message, if any. Detaching switches the sender to sending
 * subsequent responses individually.
 */

void partake_sender_attach_responsemessage(struct partake_sender *sender,
        struct partake_responsemessage *respmsg);

struct partake_responsemessage *partake_sender_detach_responsemessage(
        struct partake_sender *sender);


/*
 * "Check out" and "check in" a/the response message: task handlers use these
 * to record their response. If the sender holds a response message being
 * accumulated, that response message gets checked out. Otherwise, a new
 * response message is created and checked out, and sent upon checking back in.
 */

struct partake_responsemessage *partake_sender_checkout_responsemessage(
        struct partake_sender *sender);

void partake_sender_checkin_responsemessage(struct partake_sender *sender,
        struct partake_responsemessage *respmsg);
