/*
 * Request handling tasks
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

struct partaked_channel;
struct partaked_request;
struct partaked_sender;


int partaked_task_handle(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);


int partaked_task_Hello(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_Quit(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_GetSegment(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_Alloc(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_Realloc(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_Open(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_Close(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_Publish(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_Unpublish(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_CreateVoucher(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_DiscardVoucher(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);

int partaked_task_Unknown(struct partaked_connection *conn,
        struct partaked_request *req, struct partaked_sender *sender);
