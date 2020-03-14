/*
 * Partake request accessor functions
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

#include "partake_iobuf.h"
#include "partake_protocol_reader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


struct partake_request;

struct partake_request *partake_request_create(struct partake_iobuf *buf,
        size_t offset);

void partake_request_destroy(struct partake_request *req);

uint64_t partake_request_seqno(struct partake_request *req);

partake_protocol_Request_union_type_t partake_request_type(
        struct partake_request *req);

uint32_t partake_request_GetSegments_count(struct partake_request *req);

uint32_t partake_request_GetSegments_segment(struct partake_request *req,
        size_t index);

uint32_t partake_request_Alloc_count(struct partake_request *req);

uint64_t partake_request_Alloc_size(struct partake_request *req,
        size_t index);

bool partake_request_Alloc_clear(struct partake_request *req);

bool partake_request_Alloc_share_mutable(struct partake_request *req);

uint32_t partake_request_Realloc_count(struct partake_request *req);

uint64_t partake_request_Realloc_token(struct partake_request *req,
        size_t index);

uint64_t partake_request_Realloc_size(struct partake_request *req,
        size_t index);

uint32_t partake_request_Acquire_count(struct partake_request *req);

uint64_t partake_request_Acquire_token(struct partake_request *req,
        size_t index);

bool partake_request_Acquire_wait(struct partake_request *req);

bool partake_request_Acquire_share_mutable(struct partake_request *req);

uint32_t partake_request_Release_count(struct partake_request *req);

uint64_t partake_request_Release_token(struct partake_request *req,
        size_t index);

uint32_t partake_request_Publish_count(struct partake_request *req);

uint64_t partake_request_Publish_token(struct partake_request *req,
        size_t index);

uint32_t partake_request_Unpublish_count(struct partake_request *req);

uint64_t partake_request_Unpublish_token(struct partake_request *req,
        size_t index);

bool partake_request_Unpublish_wait(struct partake_request *req);

bool partake_request_Unpublish_clear(struct partake_request *req);
