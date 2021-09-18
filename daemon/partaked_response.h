/*
 * Partaked responses
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_token.h"

#include <stdint.h>

struct partaked_handle;
struct partaked_request;
struct partaked_segment;

struct partaked_resparray *partaked_resparray_create(void);

struct partaked_iobuf *
partaked_resparray_finish(struct partaked_resparray *resparr, size_t *size);

void partaked_resparray_append_Hello_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status, uint32_t client_no);

void partaked_resparray_append_GetSegment_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status, struct partaked_segment *segment);

void partaked_resparray_append_Alloc_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status, struct partaked_handle *handle);

void partaked_resparray_append_Realloc_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status, struct partaked_handle *handle);

void partaked_resparray_append_Open_response(struct partaked_resparray *resparr,
                                             struct partaked_request *req,
                                             int status,
                                             struct partaked_handle *handle);

void partaked_resparray_append_Close_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status);

void partaked_resparray_append_Share_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status);

void partaked_resparray_append_Unshare_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status, partaked_token token);

void partaked_resparray_append_CreateVoucher_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status, partaked_token token);

void partaked_resparray_append_DiscardVoucher_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status, partaked_token token);

void partaked_resparray_append_empty_response(
    struct partaked_resparray *resparr, struct partaked_request *req,
    int status);
