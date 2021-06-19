/*
 * Collect and send partaked responses
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
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
