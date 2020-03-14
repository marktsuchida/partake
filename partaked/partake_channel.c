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

#include "partake_channel.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_pool.h"
#include "partake_token.h"

#include <uthash.h>


// Per-channel object handle.
struct partake_object_handle {
    struct partake_object *object; // Owning pointer (reference counted)
    unsigned refcount; // Number of references held by the connection

    // All object handles are kept in a per-channel hash table.
    UT_hash_handle hh; // Key == object->token
};


struct partake_channel {
    struct partake_pool *pool; // Non-owning

    // Local table containing objects held by this channel.
    struct partake_object_handle *handles; // Hash table
};


static inline void add_handle_to_channel(struct partake_channel *chan,
        struct partake_object_handle *handle) {
    HASH_ADD_KEYPTR(hh, chan->handles, &handle->object->token,
            sizeof(partake_token), handle);
}


static inline struct partake_object_handle *find_handle_in_channel(
        struct partake_channel *chan, partake_token token) {
    struct partake_object_handle *ret;
    HASH_FIND(hh, chan->handles, &token, sizeof(partake_token), ret);
    return ret;
}


static inline void remove_handle_from_channel(struct partake_channel *chan,
        struct partake_object_handle *handle) {
    HASH_DELETE(hh, chan->handles, handle);
}


struct partake_channel *partake_channel_create(struct partake_pool *pool) {
    struct partake_channel *ret =
        partake_malloc(sizeof(struct partake_channel));

    ret->pool = pool;
    ret->handles = NULL;
    return ret;
}


void partake_channel_destroy(struct partake_channel *chan) {
    struct partake_object_handle *handle, *tmp;
    HASH_ITER(hh, chan->handles, handle, tmp) {
        struct partake_object *object = handle->object;
        --object->refcount;
        if (object->refcount == 1 &&
                object->flags & PARTAKE_OBJECT_PUBLISHED) {
            // TODO Process potential pending unpublish request
        }
        else if (object->refcount == 0) {
            partake_pool_destroy_object(chan->pool, object);
        }
        HASH_DEL(chan->handles, handle);
        partake_free(handle);
    }

    partake_free(chan);
}


int partake_channel_alloc_object(struct partake_channel *chan,
        size_t size, bool share_mutable,
        struct partake_object_handle **handle) {
    struct partake_object *object =
        partake_pool_create_object(chan->pool, size, partake_generate_token());
    if (object == NULL) {
        return -1;
    }

    if (share_mutable)
        object->flags |= PARTAKE_OBJECT_SHARE_MUTABLE;
    else
        object->exclusive_writer = chan;

    *handle = partake_malloc(sizeof(struct partake_object_handle));
    (*handle)->object = object;
    (*handle)->refcount = 1;
    add_handle_to_channel(chan, *handle);

    return 0;
}


int partake_channel_realloc_object(struct partake_channel *chan,
        partake_token token, size_t size,
        struct partake_object_handle **handle) {
    *handle = find_handle_in_channel(chan, token);
    if (*handle == NULL || (*handle)->object->exclusive_writer != chan) {
        return -1;
    }

    return partake_pool_resize_object(chan->pool, (*handle)->object, size);
}


int partake_channel_acquire_object(struct partake_channel *chan,
        partake_token token, struct partake_object_handle **handle) {
    *handle = find_handle_in_channel(chan, token);
    struct partake_object *object;
    if (*handle == NULL) {
        object = partake_pool_find_object(chan->pool, token);
        if (object == NULL) {
            return -1;
        }

        ++object->refcount;

        *handle = partake_malloc(sizeof(struct partake_object_handle));
        (*handle)->object = object;
        (*handle)->refcount = 1;
        add_handle_to_channel(chan, *handle);
    }
    else {
        ++(*handle)->refcount;
        object = (*handle)->object;
    }

    if (!(object->flags & PARTAKE_OBJECT_SHARE_MUTABLE) &&
            !(object->flags & PARTAKE_OBJECT_PUBLISHED)) {
        return -1; // TODO BUSY (handle refcount has been incremented)
    }

    return 0;
}


int partake_channel_release_object(struct partake_channel *chan,
        partake_token token) {
    struct partake_object_handle *handle = find_handle_in_channel(chan, token);
    if (handle == NULL) {
        return -1;
    }

    --handle->refcount;

    struct partake_object *object = handle->object;

    if (object->exclusive_writer == chan) {
        // Releasing an unpublished object owned by this channel
        object->exclusive_writer = NULL;

        // TODO Process (cancel) any pending acquire requests
    }
    else if (object->flags & PARTAKE_OBJECT_PUBLISHED &&
            handle->refcount == 1 && object->refcount == 1) {
        // TODO Process any pending unpublish request on this channel
    }

    if (handle->refcount == 0) {
        remove_handle_from_channel(chan, handle);
        partake_free(handle);

        --object->refcount;

        if (object->refcount == 1) {
            // TODO Process any pending unpublish request on other channels
        }

        if (object->refcount == 0)
            partake_pool_destroy_object(chan->pool, object);
    }

    return 0;
}


int partake_channel_publish_object(struct partake_channel *chan,
        partake_token token) {
    struct partake_object_handle *handle = find_handle_in_channel(chan, token);
    if (handle == NULL) {
        return -1;
    }
    if (handle->object->flags & PARTAKE_OBJECT_PUBLISHED ||
            handle->object->flags & PARTAKE_OBJECT_SHARE_MUTABLE) {
        return -1;
    }

    handle->object->flags |= PARTAKE_OBJECT_PUBLISHED;

    // TODO Process any pending acquire requests

    return 0;
}


int partake_channel_unpublish_object(struct partake_channel *chan,
        partake_token token, struct partake_object_handle **handle) {
    *handle = find_handle_in_channel(chan, token);
    if (*handle == NULL) {
        return -1;
    }

    struct partake_object *object = (*handle)->object;
    if (!(object->flags & PARTAKE_OBJECT_PUBLISHED)) {
        return -1;
    }

    if ((*handle)->refcount > 1 || object->refcount > 1) {
        return -1; // TODO BUSY
    }

    remove_handle_from_channel(chan, *handle);

    partake_pool_rekey_object(chan->pool, object, partake_generate_token());

    object->flags &= ~PARTAKE_OBJECT_PUBLISHED;

    add_handle_to_channel(chan, *handle);

    return 0;
}
