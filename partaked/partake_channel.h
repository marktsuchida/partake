/*
 * Connection access to objects
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

#include <stdbool.h>
#include <stddef.h>

struct partake_handle;
struct partake_pool;


struct partake_channel *partake_channel_create(struct partake_pool *pool);

void partake_channel_destroy(struct partake_channel *chan);


// If successful, *handle has refcount 1, open_count 1.
int partake_channel_alloc_object(struct partake_channel *chan,
        size_t size, bool clear, bool share_mutable,
        struct partake_handle **handle);


// Refcount of *handle is unchanged.
int partake_channel_realloc_object(struct partake_channel *chan,
        partake_token token, size_t size, struct partake_handle **handle);


// If successful, handle open_count is incremented.
int partake_channel_resume_open_object(struct partake_channel *chan,
        struct partake_handle *handle);

// If successful, *handle refcount and open_count are incremented; if busy,
// only refcount is incremented.
int partake_channel_open_object(struct partake_channel *chan,
        partake_token token, bool share_mutable,
        struct partake_handle **handle);


// If successful, handle for token has refcount and open_count decremented.
int partake_channel_close_object(struct partake_channel *chan,
        partake_token token);


// Refcount for handle for token is unchanged.
int partake_channel_publish_object(struct partake_channel *chan,
        partake_token token);


// Refcount of handle is unchanged.
int partake_channel_resume_unpublish_object(struct partake_channel *chan,
        struct partake_handle *handle, bool clear);

// Refcount of *handle is unchanged; caller must retain if suspending
int partake_channel_unpublish_object(struct partake_channel *chan,
        partake_token token, bool clear, struct partake_handle **handle);


// Decrement refcount of handle.
void partake_channel_release_handle(struct partake_channel *chan,
        struct partake_handle *handle);
