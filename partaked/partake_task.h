/*
 * Request handling tasks
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

struct partake_channel;
struct partake_request;
struct partake_sender;


int partake_task_handle(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);


int partake_task_Hello(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_Quit(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_GetSegment(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_Alloc(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_Realloc(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_Open(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_Close(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_Publish(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_Unpublish(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_CreateVoucher(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_DiscardVoucher(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);

int partake_task_Unknown(struct partake_connection *conn,
        struct partake_request *req, struct partake_sender *sender);
