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

#include "prefix.h"

#include "partake_channel.h"
#include "partake_handle.h"
#include "partake_malloc.h"
#include "partake_object.h"
#include "partake_pool.h"
#include "partake_protocol_reader.h"
#include "partake_token.h"
#include "partake_voucherqueue.h"

#include <uthash.h>
#include <utlist.h>


struct partake_channel {
    struct partake_pool *pool; // Non-owning

    // Local table containing objects held by this channel.
    struct partake_handle *handles; // Hash table
};


static inline void add_handle_to_channel(struct partake_channel *chan,
        struct partake_handle *handle) {
    HASH_ADD_KEYPTR(hh, chan->handles, &handle->object->token,
            sizeof(partake_token), handle);
}


static inline struct partake_handle *find_handle_in_channel(
        struct partake_channel *chan, partake_token token) {
    struct partake_handle *ret;
    HASH_FIND(hh, chan->handles, &token, sizeof(partake_token), ret);
    return ret;
}


static inline void remove_handle_from_channel(struct partake_channel *chan,
        struct partake_handle *handle) {
    HASH_DELETE(hh, chan->handles, handle);
}


static void close_object_impl(struct partake_channel *chan,
        struct partake_handle *handle);


struct partake_channel *partake_channel_create(struct partake_pool *pool) {
    struct partake_channel *ret = partake_malloc(sizeof(*ret));

    ret->pool = pool;
    ret->handles = NULL;
    return ret;
}


void partake_channel_destroy(struct partake_channel *chan) {
    struct partake_handle *handle, *tmp;
    HASH_ITER(hh, chan->handles, handle, tmp) {
        // Retain the handle, in case our only reference is via a pending
        // request.
        ++handle->refcount;

        // Cancel waits first so they don't fire upon releasing.
        partake_handle_cancel_all_continue_on_publish(handle);
        partake_handle_cancel_any_continue_on_sole_ownership(handle);
        assert (handle->open_count == handle->refcount - 1);

        while (handle->open_count > 0)
            close_object_impl(chan, handle);

        partake_channel_release_handle(chan, handle);
    }
    assert (chan->handles == NULL);

    partake_free(chan);
}


int partake_channel_get_segment(struct partake_channel *chan, uint32_t segno,
        struct partake_segment **segment) {
    // Currently we use a single segment, 0.
    if (segno != 0)
        return partake_protocol_Status_NO_SUCH_SEGMENT;

    *segment = partake_pool_segment(chan->pool);
    return 0;
}


struct partake_pool *partake_channel_get_pool(struct partake_channel *chan) {
    return chan->pool;
}


int partake_channel_alloc_object(struct partake_channel *chan,
        size_t size, bool clear, uint8_t policy,
        struct partake_handle **handle) {
    struct partake_object *object =
        partake_pool_create_object(chan->pool, size, clear,
                partake_generate_token());
    if (object == NULL)
        return partake_protocol_Status_OUT_OF_MEMORY;

    partake_object_flags_set_policy(&object->flags, policy);

    if (policy == partake_protocol_Policy_STANDARD)
        object->exclusive_writer = chan;

    object->open_count = 1;

    *handle = partake_calloc(1, sizeof(**handle));
    (*handle)->object = object;
    (*handle)->refcount = 1;
    (*handle)->open_count = 1;
    add_handle_to_channel(chan, *handle);

    return 0;
}


int partake_channel_realloc_object(struct partake_channel *chan,
        partake_token token, size_t size, struct partake_handle **handle) {
    *handle = find_handle_in_channel(chan, token);
    if (*handle == NULL || (*handle)->object->exclusive_writer != chan)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    if (partake_pool_resize_object(chan->pool, (*handle)->object, size) != 0)
        return partake_protocol_Status_OUT_OF_MEMORY;

    return 0;
}


int partake_channel_resume_open_object(struct partake_channel *chan,
        struct partake_handle *handle, struct partake_object *voucher) {
    if (voucher) {
        --voucher->target->refcount;
        partake_pool_destroy_object(chan->pool, voucher);
    }

    if (!(handle->object->flags & PARTAKE_OBJECT_PUBLISHED))
        return partake_protocol_Status_NO_SUCH_OBJECT;

    ++handle->open_count;
    if (handle->open_count == 1)
        ++handle->object->open_count;

    return 0;
}


int partake_channel_open_object(struct partake_channel *chan,
        partake_token token, uint8_t policy,
        struct partake_handle **handle, struct partake_object **voucher) {
    *handle = find_handle_in_channel(chan, token);
    *voucher = NULL;
    struct partake_object *object;
    if (*handle == NULL) {
        object = partake_pool_find_object(chan->pool, token);
        if (object == NULL) {
            return partake_protocol_Status_NO_SUCH_OBJECT;
        }
        if (object->flags & PARTAKE_OBJECT_IS_VOUCHER) {
            *voucher = object;
            object = (*voucher)->target;
        }
        if (policy != partake_object_flags_get_policy(object->flags)) {
            return partake_protocol_Status_NO_SUCH_OBJECT;
        }

        ++object->refcount;

        *handle = partake_calloc(1, sizeof(**handle));
        (*handle)->object = object;
        (*handle)->refcount = 1;
        add_handle_to_channel(chan, *handle);
    }
    else {
        object = (*handle)->object;
        if (policy != partake_object_flags_get_policy(object->flags)) {
            return partake_protocol_Status_NO_SUCH_OBJECT;
        }

        ++(*handle)->refcount;
    }

    if (policy == partake_protocol_Policy_STANDARD &&
        !(object->flags & PARTAKE_OBJECT_PUBLISHED))
        return partake_protocol_Status_OBJECT_BUSY;

    if (*voucher) {
        partake_voucherqueue_remove(partake_pool_get_voucherqueue(chan->pool),
                *voucher);
    }

    int status = partake_channel_resume_open_object(chan, *handle, *voucher);
    *voucher = NULL;
    return status;
}


static void close_object_impl(struct partake_channel *chan,
        struct partake_handle *handle) {
    struct partake_object *object = handle->object;

    --handle->open_count;
    if (handle->open_count == 0)
        --object->open_count;

    if (object->exclusive_writer == chan) {
        object->exclusive_writer = NULL;
        partake_handle_fire_on_publish(object);
    }
    else if (handle->open_count == 0 &&
            object->handle_waiting_for_sole_ownership == handle) {
        // We have (incorrectly) closed a handle on which we have a pending
        // Unpublish request. Trigger failure of the pending request.
        partake_handle_local_fire_on_sole_ownership(handle);
    }
    else if (object->open_count == 1 &&
            object->handle_waiting_for_sole_ownership != NULL &&
            object->handle_waiting_for_sole_ownership->open_count == 1) {
        partake_handle_fire_on_sole_ownership(handle->object);
    }

    partake_channel_release_handle(chan, handle);
}


int partake_channel_close_object(struct partake_channel *chan,
        partake_token token) {
    struct partake_handle *handle = find_handle_in_channel(chan, token);
    if (handle == NULL || handle->open_count == 0)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    close_object_impl(chan, handle);
    return 0;
}


int partake_channel_publish_object(struct partake_channel *chan,
        partake_token token) {
    struct partake_handle *handle = find_handle_in_channel(chan, token);
    if (handle == NULL || handle->object->exclusive_writer != chan)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    handle->object->flags |= PARTAKE_OBJECT_PUBLISHED;

    partake_handle_fire_on_publish(handle->object);

    return 0;
}


int partake_channel_resume_unpublish_object(struct partake_channel *chan,
        struct partake_handle *handle, bool clear) {
    if (handle->open_count == 0)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    struct partake_object *object = handle->object;
    assert (handle->open_count == 1 && object->open_count == 1);

    remove_handle_from_channel(chan, handle);

    partake_pool_rekey_object(chan->pool, object, partake_generate_token());
    if (clear)
        partake_pool_clear_object(chan->pool, object);

    object->flags &= ~PARTAKE_OBJECT_PUBLISHED;
    object->exclusive_writer = chan;

    add_handle_to_channel(chan, handle);

    return 0;
}


int partake_channel_unpublish_object(struct partake_channel *chan,
        partake_token token, bool clear, struct partake_handle **handle) {
    *handle = find_handle_in_channel(chan, token);
    if (*handle == NULL || (*handle)->open_count == 0)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    struct partake_object *object = (*handle)->object;
    if (!(object->flags & PARTAKE_OBJECT_PUBLISHED))
        return partake_protocol_Status_NO_SUCH_OBJECT;

    if ((*handle)->open_count > 1 || object->open_count > 1)
        return partake_protocol_Status_OBJECT_BUSY;

    return partake_channel_resume_unpublish_object(chan, *handle, clear);
}


int partake_channel_create_voucher(struct partake_channel *chan,
        partake_token target_token, struct partake_object **voucher) {
    // There is no logical need to search within this channel, but it is
    // expected that in most cases the object will be found in this channel.
    struct partake_handle *handle = find_handle_in_channel(chan, target_token);
    struct partake_object *object;
    if (handle == NULL) {
        object = partake_pool_find_object(chan->pool, target_token);
        if (object == NULL) {
            return partake_protocol_Status_NO_SUCH_OBJECT;
        }

        if (object->flags & PARTAKE_OBJECT_IS_VOUCHER) {
            object = object->target;
        }
    }
    else {
        object = handle->object;
        // If found in the channel, we know it is not a voucher.
    }

    partake_token token = partake_generate_token();
    *voucher = partake_pool_create_voucher(chan->pool, token, object);

    ++object->refcount;

    partake_voucherqueue_enqueue(partake_pool_get_voucherqueue(chan->pool),
            *voucher);

    return 0;
}


int partake_channel_discard_voucher(struct partake_channel *chan,
        partake_token token, struct partake_object **target) {
    struct partake_object *object = partake_pool_find_object(chan->pool, token);
    if (object == NULL) {
        return partake_protocol_Status_NO_SUCH_OBJECT;
    }

    if (object->flags & PARTAKE_OBJECT_IS_VOUCHER) {
        partake_voucherqueue_remove(partake_pool_get_voucherqueue(chan->pool),
                object);
        *target = object->target;
        --(*target)->refcount;
        partake_pool_destroy_object(chan->pool, object);
    }
    else {
        *target = object;
    }

    return 0;
}


void partake_channel_release_handle(struct partake_channel *chan,
        struct partake_handle *handle) {
    struct partake_object *object = handle->object;

    --handle->refcount;

    if (handle->refcount == 0) {
        assert (handle->open_count == 0);
        remove_handle_from_channel(chan, handle);
        partake_free(handle);

        --object->refcount;

        if (object->refcount == 0) {
            assert (object->open_count == 0);
            partake_pool_destroy_object(chan->pool, object);
        }
    }
}
