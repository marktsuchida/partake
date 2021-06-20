/*
 * Partake request accessor functions
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partaked_iobuf.h"

#include "partake_protocol_reader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Look in 'iobuf' at offset 'start', where 'size' bytes are valid, to see what
// we have. Return true if there is a complete or partial message frame; false
// otherwise (i.e. size == 0). When returning true, sets *frame_complete to
// whether the entire message frame is contained within the 'size' bytes, and
// *frame_size to the size of the message frame (including the size prefix and
// any required padding bytes). If a partial message frame is present but its
// size is not known yet, *frame_size is set to zero.
bool partake_requestframe_scan(struct partake_iobuf *iobuf, size_t start,
        size_t size, size_t *frame_size, bool *frame_complete);


// Either do nothing and return iobuf, or copy the message given by (start,
// size) from iobuf to a new buffer, release iobuf, and return the new buffer.
// *start is set to the new start of the message (i.e. zero) if copied.
struct partake_iobuf *partake_requestframe_maybe_move(
        struct partake_iobuf *iobuf, size_t *start, size_t size);


// Wrap a portion of 'buf' so that it can be accessed as a request. The
// 'offset' argument should point to the size prefix of the message frame.
struct partake_reqarray *partake_reqarray_create(struct partake_iobuf *buf,
        size_t offset);

void partake_reqarray_destroy(struct partake_reqarray *reqarr);

uint32_t partake_reqarray_count(struct partake_reqarray *reqarr);

// The returned request is uniquely owned by the caller
struct partake_request *partake_reqarray_request(
        struct partake_reqarray *reqarr, uint32_t index);

void partake_request_destroy(struct partake_request *req);


uint64_t partake_request_seqno(struct partake_request *req);

partake_protocol_AnyRequest_union_type_t partake_request_type(
        struct partake_request *req);


uint32_t partake_request_Hello_pid(struct partake_request *req);

const char *partake_request_Hello_name(struct partake_request *req);

uint32_t partake_request_GetSegment_segment(struct partake_request *req);

uint64_t partake_request_Alloc_size(struct partake_request *req);

bool partake_request_Alloc_clear(struct partake_request *req);

uint8_t partake_request_Alloc_policy(struct partake_request *req);

uint64_t partake_request_Realloc_token(struct partake_request *req);

uint64_t partake_request_Realloc_size(struct partake_request *req);

uint64_t partake_request_Open_token(struct partake_request *req);

bool partake_request_Open_wait(struct partake_request *req);

uint8_t partake_request_Open_policy(struct partake_request *req);

uint64_t partake_request_Close_token(struct partake_request *req);

uint64_t partake_request_Publish_token(struct partake_request *req);

uint64_t partake_request_Unpublish_token(struct partake_request *req);

bool partake_request_Unpublish_wait(struct partake_request *req);

bool partake_request_Unpublish_clear(struct partake_request *req);

uint64_t partake_request_CreateVoucher_token(struct partake_request *req);

uint64_t partake_request_DiscardVoucher_token(struct partake_request *req);