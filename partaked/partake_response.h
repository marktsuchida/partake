/*
 * Partaked responses
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partake_token.h"

#include <stdint.h>

struct partake_handle;
struct partake_request;
struct partake_segment;


struct partake_resparray *partake_resparray_create(void);


struct partake_iobuf *partake_resparray_finish(
        struct partake_resparray *resparr, size_t *size);


void partake_resparray_append_Hello_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, uint32_t client_no);

void partake_resparray_append_GetSegment_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, struct partake_segment *segment);

void partake_resparray_append_Alloc_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, struct partake_handle *handle);

void partake_resparray_append_Realloc_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, struct partake_handle *handle);

void partake_resparray_append_Open_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, struct partake_handle *handle);

void partake_resparray_append_Close_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status);

void partake_resparray_append_Publish_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status);

void partake_resparray_append_Unpublish_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, partake_token token);

void partake_resparray_append_CreateVoucher_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, partake_token token);

void partake_resparray_append_DiscardVoucher_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status, partake_token token);

void partake_resparray_append_empty_response(
        struct partake_resparray *resparr, struct partake_request *req,
        int status);
