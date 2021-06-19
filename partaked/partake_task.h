/*
 * Request handling tasks
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
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
