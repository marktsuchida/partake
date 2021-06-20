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
bool partaked_requestframe_scan(struct partaked_iobuf *iobuf, size_t start,
        size_t size, size_t *frame_size, bool *frame_complete);


// Either do nothing and return iobuf, or copy the message given by (start,
// size) from iobuf to a new buffer, release iobuf, and return the new buffer.
// *start is set to the new start of the message (i.e. zero) if copied.
struct partaked_iobuf *partaked_requestframe_maybe_move(
        struct partaked_iobuf *iobuf, size_t *start, size_t size);


// Wrap a portion of 'buf' so that it can be accessed as a request. The
// 'offset' argument should point to the size prefix of the message frame.
struct partaked_reqarray *partaked_reqarray_create(struct partaked_iobuf *buf,
        size_t offset);

void partaked_reqarray_destroy(struct partaked_reqarray *reqarr);

uint32_t partaked_reqarray_count(struct partaked_reqarray *reqarr);

// The returned request is uniquely owned by the caller
struct partaked_request *partaked_reqarray_request(
        struct partaked_reqarray *reqarr, uint32_t index);

void partaked_request_destroy(struct partaked_request *req);


uint64_t partaked_request_seqno(struct partaked_request *req);

partake_protocol_AnyRequest_union_type_t partaked_request_type(
        struct partaked_request *req);


uint32_t partaked_request_Hello_pid(struct partaked_request *req);

const char *partaked_request_Hello_name(struct partaked_request *req);

uint32_t partaked_request_GetSegment_segment(struct partaked_request *req);

uint64_t partaked_request_Alloc_size(struct partaked_request *req);

bool partaked_request_Alloc_clear(struct partaked_request *req);

uint8_t partaked_request_Alloc_policy(struct partaked_request *req);

uint64_t partaked_request_Realloc_token(struct partaked_request *req);

uint64_t partaked_request_Realloc_size(struct partaked_request *req);

uint64_t partaked_request_Open_token(struct partaked_request *req);

bool partaked_request_Open_wait(struct partaked_request *req);

uint8_t partaked_request_Open_policy(struct partaked_request *req);

uint64_t partaked_request_Close_token(struct partaked_request *req);

uint64_t partaked_request_Publish_token(struct partaked_request *req);

uint64_t partaked_request_Unpublish_token(struct partaked_request *req);

bool partaked_request_Unpublish_wait(struct partaked_request *req);

bool partaked_request_Unpublish_clear(struct partaked_request *req);

uint64_t partaked_request_CreateVoucher_token(struct partaked_request *req);

uint64_t partaked_request_DiscardVoucher_token(struct partaked_request *req);
