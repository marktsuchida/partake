/*
 * Partaked responses
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
