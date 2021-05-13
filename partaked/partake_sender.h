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

#include <stdbool.h>

struct partake_resparray;


struct partake_sender *partake_sender_create(uv_stream_t *client);

struct partake_sender *partake_sender_incref(struct partake_sender *sender);

void partake_sender_decref(struct partake_sender *sender);


// Send any accumulated responses.
void partake_sender_flush(struct partake_sender *sender);

void partake_sender_set_autoflush(struct partake_sender *sender,
        bool autoflush);


/*
 * "Check out" and "check in" a/the response array: task handlers use these to
 * record their response. A checkout always returns a valid resparray. A
 * checkin indicates that the caller is done adding a response(s). If the
 * sender is set to autoflush, the response(s) will be sent upon checking in;
 * otherwise some other code should call partake_sender_flush() at an
 * appropriate moment.
 */

struct partake_resparray *partake_sender_checkout_resparray(
        struct partake_sender *sender);

void partake_sender_checkin_resparray(struct partake_sender *sender,
        struct partake_resparray *resparr);
