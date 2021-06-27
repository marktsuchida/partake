/*
 * Collect and send partaked responses
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>

struct partaked_resparray;

struct partaked_sender *partaked_sender_create(uv_stream_t *client);

struct partaked_sender *partaked_sender_incref(struct partaked_sender *sender);

void partaked_sender_decref(struct partaked_sender *sender);

// Send any accumulated responses.
void partaked_sender_flush(struct partaked_sender *sender);

void partaked_sender_set_autoflush(struct partaked_sender *sender,
                                   bool autoflush);

/*
 * "Check out" and "check in" a/the response array: task handlers use these to
 * record their response. A checkout always returns a valid resparray. A
 * checkin indicates that the caller is done adding a response(s). If the
 * sender is set to autoflush, the response(s) will be sent upon checking in;
 * otherwise some other code should call partaked_sender_flush() at an
 * appropriate moment.
 */

struct partaked_resparray *
partaked_sender_checkout_resparray(struct partaked_sender *sender);

void partaked_sender_checkin_resparray(struct partaked_sender *sender,
                                       struct partaked_resparray *resparr);
