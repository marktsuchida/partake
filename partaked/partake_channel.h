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

struct partake_channel;
struct partake_object_handle;
struct partake_pool;


struct partake_channel *partake_channel_create(struct partake_pool *pool);

void partake_channel_destroy(struct partake_channel *chan);


int partake_channel_alloc_object(struct partake_channel *chan,
        size_t size, bool share_mutable,
        struct partake_object_handle **handle);

int partake_channel_realloc_object(struct partake_channel *chan,
        partake_token token, size_t size,
        struct partake_object_handle **handle);

int partake_channel_acquire_object(struct partake_channel *chan,
        partake_token token, struct partake_object_handle **handle);

int partake_channel_release_object(struct partake_channel *chan,
        partake_token token);

int partake_channel_publish_object(struct partake_channel *chan,
        partake_token token);

int partake_channel_unpublish_object(struct partake_channel *chan,
        partake_token token, struct partake_object_handle **handle);
