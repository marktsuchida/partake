/*
 * Connection access to objects
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_channel.h"
#include "partaked_handle.h"
#include "partaked_malloc.h"
#include "partaked_object.h"
#include "partaked_pool.h"
#include "partaked_token.h"
#include "partaked_voucherqueue.h"

#include "partake_protocol_reader.h"

#include <uthash.h>
#include <utlist.h>

struct partaked_channel {
    struct partaked_pool *pool; // Non-owning

    // Local table containing objects held by this channel.
    struct partaked_handle *handles; // Hash table
};

static inline void add_handle_to_channel(struct partaked_channel *chan,
                                         struct partaked_handle *handle) {
    HASH_ADD_KEYPTR(hh, chan->handles, &handle->object->token,
                    sizeof(partaked_token), handle);
}

static inline struct partaked_handle *
find_handle_in_channel(struct partaked_channel *chan, partaked_token token) {
    struct partaked_handle *ret;
    HASH_FIND(hh, chan->handles, &token, sizeof(partaked_token), ret);
    return ret;
}

static inline void remove_handle_from_channel(struct partaked_channel *chan,
                                              struct partaked_handle *handle) {
    HASH_DELETE(hh, chan->handles, handle);
}

static void close_object_impl(struct partaked_channel *chan,
                              struct partaked_handle *handle);

struct partaked_channel *partaked_channel_create(struct partaked_pool *pool) {
    struct partaked_channel *ret = partaked_malloc(sizeof(*ret));

    ret->pool = pool;
    ret->handles = NULL;
    return ret;
}

void partaked_channel_destroy(struct partaked_channel *chan) {
    struct partaked_handle *handle, *tmp;
    HASH_ITER(hh, chan->handles, handle, tmp) {
        // Retain the handle, in case our only reference is via a pending
        // request.
        ++handle->refcount;

        // Cancel waits first so they don't fire upon releasing.
        partaked_handle_cancel_all_continue_on_share(handle);
        partaked_handle_cancel_any_continue_on_sole_ownership(handle);
        assert(handle->open_count == handle->refcount - 1);

        while (handle->open_count > 0)
            close_object_impl(chan, handle);

        partaked_channel_release_handle(chan, handle);
    }
    assert(chan->handles == NULL);

    partaked_free(chan);
}

int partaked_channel_get_segment(struct partaked_channel *chan, uint32_t segno,
                                 struct partaked_segment **segment) {
    // Currently we use a single segment, 0.
    if (segno != 0)
        return partake_protocol_Status_NO_SUCH_SEGMENT;

    *segment = partaked_pool_segment(chan->pool);
    return 0;
}

struct partaked_pool *partaked_channel_get_pool(struct partaked_channel *chan) {
    return chan->pool;
}

int partaked_channel_alloc_object(struct partaked_channel *chan, size_t size,
                                  bool clear, uint8_t policy,
                                  struct partaked_handle **handle) {
    struct partaked_object *object = partaked_pool_create_object(
        chan->pool, size, clear, partaked_generate_token());
    if (object == NULL)
        return partake_protocol_Status_OUT_OF_SHMEM;

    partaked_object_flags_set_policy(&object->flags, policy);

    if (policy == partake_protocol_Policy_REGULAR)
        object->exclusive_writer = chan;

    object->open_count = 1;

    *handle = partaked_calloc(1, sizeof(**handle));
    (*handle)->object = object;
    (*handle)->refcount = 1;
    (*handle)->open_count = 1;
    add_handle_to_channel(chan, *handle);

    return 0;
}

int partaked_channel_resume_open_object(struct partaked_channel *chan,
                                        struct partaked_handle *handle,
                                        struct partaked_object *voucher) {
    if (voucher) {
        --voucher->target->refcount;
        partaked_pool_destroy_object(chan->pool, voucher);
    }

    if (!(handle->object->flags & PARTAKED_OBJECT_SHARED))
        return partake_protocol_Status_NO_SUCH_OBJECT;

    ++handle->open_count;
    if (handle->open_count == 1)
        ++handle->object->open_count;

    return 0;
}

int partaked_channel_open_object(struct partaked_channel *chan,
                                 partaked_token token, uint8_t policy,
                                 struct partaked_handle **handle,
                                 struct partaked_object **voucher) {
    *handle = find_handle_in_channel(chan, token);
    *voucher = NULL;
    struct partaked_object *object;
    if (*handle == NULL) {
        object = partaked_pool_find_object(chan->pool, token);
        if (object == NULL) {
            return partake_protocol_Status_NO_SUCH_OBJECT;
        }
        if (object->flags & PARTAKED_OBJECT_IS_VOUCHER) {
            *voucher = object;
            object = (*voucher)->target;
        }
        if (policy != partaked_object_flags_get_policy(object->flags)) {
            return partake_protocol_Status_NO_SUCH_OBJECT;
        }

        ++object->refcount;

        *handle = partaked_calloc(1, sizeof(**handle));
        (*handle)->object = object;
        (*handle)->refcount = 1;
        add_handle_to_channel(chan, *handle);
    } else {
        object = (*handle)->object;
        if (policy != partaked_object_flags_get_policy(object->flags)) {
            return partake_protocol_Status_NO_SUCH_OBJECT;
        }

        ++(*handle)->refcount;
    }

    if (policy == partake_protocol_Policy_REGULAR &&
        !(object->flags & PARTAKED_OBJECT_SHARED))
        return partake_protocol_Status_OBJECT_BUSY;

    if (*voucher) {
        partaked_voucherqueue_remove(partaked_pool_get_voucherqueue(chan->pool),
                                     *voucher);
    }

    int status = partaked_channel_resume_open_object(chan, *handle, *voucher);
    *voucher = NULL;
    return status;
}

static void close_object_impl(struct partaked_channel *chan,
                              struct partaked_handle *handle) {
    struct partaked_object *object = handle->object;

    --handle->open_count;
    if (handle->open_count == 0)
        --object->open_count;

    if (object->exclusive_writer == chan) {
        object->exclusive_writer = NULL;
        partaked_handle_fire_on_share(object);
    } else if (handle->open_count == 0 &&
               object->handle_waiting_for_sole_ownership == handle) {
        // We have (incorrectly) closed a handle on which we have a pending
        // Unshare request. Trigger failure of the pending request.
        partaked_handle_local_fire_on_sole_ownership(handle);
    } else if (object->open_count == 1 &&
               object->handle_waiting_for_sole_ownership != NULL &&
               object->handle_waiting_for_sole_ownership->open_count == 1) {
        partaked_handle_fire_on_sole_ownership(handle->object);
    }

    partaked_channel_release_handle(chan, handle);
}

int partaked_channel_close_object(struct partaked_channel *chan,
                                  partaked_token token) {
    struct partaked_handle *handle = find_handle_in_channel(chan, token);
    if (handle == NULL || handle->open_count == 0)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    close_object_impl(chan, handle);
    return 0;
}

int partaked_channel_share_object(struct partaked_channel *chan,
                                  partaked_token token) {
    struct partaked_handle *handle = find_handle_in_channel(chan, token);
    if (handle == NULL || handle->object->exclusive_writer != chan)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    handle->object->flags |= PARTAKED_OBJECT_SHARED;

    partaked_handle_fire_on_share(handle->object);

    return 0;
}

int partaked_channel_resume_unshare_object(struct partaked_channel *chan,
                                           struct partaked_handle *handle,
                                           bool clear) {
    if (handle->open_count == 0)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    struct partaked_object *object = handle->object;
    assert(handle->open_count == 1 && object->open_count == 1);

    remove_handle_from_channel(chan, handle);

    partaked_pool_rekey_object(chan->pool, object, partaked_generate_token());
    if (clear)
        partaked_pool_clear_object(chan->pool, object);

    object->flags &= ~PARTAKED_OBJECT_SHARED;
    object->exclusive_writer = chan;

    add_handle_to_channel(chan, handle);

    return 0;
}

int partaked_channel_unshare_object(struct partaked_channel *chan,
                                    partaked_token token, bool clear,
                                    struct partaked_handle **handle) {
    *handle = find_handle_in_channel(chan, token);
    if (*handle == NULL || (*handle)->open_count == 0)
        return partake_protocol_Status_NO_SUCH_OBJECT;

    struct partaked_object *object = (*handle)->object;
    if (!(object->flags & PARTAKED_OBJECT_SHARED))
        return partake_protocol_Status_NO_SUCH_OBJECT;

    if ((*handle)->open_count > 1 || object->open_count > 1)
        return partake_protocol_Status_OBJECT_BUSY;

    return partaked_channel_resume_unshare_object(chan, *handle, clear);
}

int partaked_channel_create_voucher(struct partaked_channel *chan,
                                    partaked_token target_token,
                                    struct partaked_object **voucher) {
    // There is no logical need to search within this channel, but it is
    // expected that in most cases the object will be found in this channel.
    struct partaked_handle *handle = find_handle_in_channel(chan, target_token);
    struct partaked_object *object;
    if (handle == NULL) {
        object = partaked_pool_find_object(chan->pool, target_token);
        if (object == NULL) {
            return partake_protocol_Status_NO_SUCH_OBJECT;
        }

        if (object->flags & PARTAKED_OBJECT_IS_VOUCHER) {
            object = object->target;
        }
    } else {
        object = handle->object;
        // If found in the channel, we know it is not a voucher.
    }

    partaked_token token = partaked_generate_token();
    *voucher = partaked_pool_create_voucher(chan->pool, token, object);

    ++object->refcount;

    partaked_voucherqueue_enqueue(partaked_pool_get_voucherqueue(chan->pool),
                                  *voucher);

    return 0;
}

int partaked_channel_discard_voucher(struct partaked_channel *chan,
                                     partaked_token token,
                                     struct partaked_object **target) {
    struct partaked_object *object =
        partaked_pool_find_object(chan->pool, token);
    if (object == NULL) {
        return partake_protocol_Status_NO_SUCH_OBJECT;
    }

    if (object->flags & PARTAKED_OBJECT_IS_VOUCHER) {
        partaked_voucherqueue_remove(partaked_pool_get_voucherqueue(chan->pool),
                                     object);
        *target = object->target;
        --(*target)->refcount;
        partaked_pool_destroy_object(chan->pool, object);
    } else {
        *target = object;
    }

    return 0;
}

void partaked_channel_release_handle(struct partaked_channel *chan,
                                     struct partaked_handle *handle) {
    struct partaked_object *object = handle->object;

    --handle->refcount;

    if (handle->refcount == 0) {
        assert(handle->open_count == 0);
        remove_handle_from_channel(chan, handle);
        partaked_free(handle);

        --object->refcount;

        if (object->refcount == 0) {
            assert(object->open_count == 0);
            partaked_pool_destroy_object(chan->pool, object);
        }
    }
}
