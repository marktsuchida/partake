/*
 * Partake object descriptor
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

#include "partake_malloc.h" // before uthash.h
#include "partake_token.h"

#include <uthash.h>

#include <stddef.h>
#include <stdint.h>

struct partake_channel;


/*
 * STANDARD objects start their life unpublished. They may get published once.
 * Objects are reference counted, and they are deallocated when the count
 * reaches zero. Unpublished objects are not sharable, but their reference count
 * may become greater than 1 if others are waiting for publication.
 * Unpublishing is equivalent to deallocating and allocating a new object,
 * except that the old object's buffer is reused; therefore it can only be
 * performed when the reference count is 1.
 *
 * PRIMITIVE objects are immediately sharable and cannot be published or
 * unpublished. Acquiring the object returns a writable reference.
 */


// Object flags
enum {
    PARTAKE_OBJECT_IS_VOUCHER = 1 << 0,

    // Currently we only have 2 possible values for policy, so we use 1 bit.
    // The 1-bit values are defined by the protocol.
    PARTAKE_OBJECT_POLICY_SHIFT = 1,
    PARTAKE_OBJECT_POLICY_MASK = 1 << PARTAKE_OBJECT_POLICY_SHIFT,

    PARTAKE_OBJECT_PUBLISHED = 1 << 2,
};

static inline uint8_t partake_object_flags_get_policy(short flags) {
    return (flags & PARTAKE_OBJECT_POLICY_MASK) >> PARTAKE_OBJECT_POLICY_SHIFT;
}

static inline void partake_object_flags_set_policy(short *flags,
    uint8_t policy) {
    *flags &= ~PARTAKE_OBJECT_POLICY_MASK;
    *flags |= (policy << PARTAKE_OBJECT_POLICY_SHIFT) &
        PARTAKE_OBJECT_POLICY_MASK;
}


// Object descriptor
struct partake_object {
    partake_token token;

    // Object type, policy, and state.
    short flags;

    // Object descriptors are kept in a global hash table for their entire
    // lifetime. This reference is _not_ included in the refcount field.
    UT_hash_handle hh; // Key == token

    // The remaining fields depend on (flags & PARTAKE_OBJECT_IS_VOUCHER).
    union {
        // Object
        struct {
            /* unsigned segment; */ // Currently always 0
            size_t offset;          // Relative to segment base address
            size_t size;

            // The reference count is the number of handles to this object. This
            // includes channels waiting for this object to change state. (Note
            // that a single channel can hold multiple references to an object;
            // the object doesn't know about this.) For vouchers, this is always
            // 1.
            unsigned refcount;

            // The open count is the number of handles to this object with a
            // positive open count. For vouchers, this is always 0.
            unsigned open_count;

            // The channel currently holding a writable reference. Always NULL
            // for published or PRIMITIVE objects and for vouchers.
            struct partake_channel *exclusive_writer;

            // Some request handling requires waiting on objects to change
            // state. Here we only manage handles waiting on this object;
            // detailed bookkeeping and callbacks are done by handles.

            // These handles are notified when this object is published, or
            // released by the creator without publishing. The list is empty
            // unless this object is unpublished. Handles are stored in a
            // singly-linked list (utlist).
            struct partake_handle *handles_waiting_for_publish;

            // This handle is notified when this object is published _and_ the
            // total number of owning handles and vouchers goes from 2 to 1. It
            // will be NULL unless the object is published and there is more
            // than 1 owning handle.
            struct partake_handle *handle_waiting_for_sole_ownership;
        };

        // Voucher
        struct {
            // Target object of the voucher. The target is a regular object,
            // never a voucher itself.
            struct partake_object *target;

            // Expiration time in ms (uv_now() value).
            uint64_t expiration;

            // Doubly-linked list pointers for voucher queue.
            struct partake_object *next;
            struct partake_object *prev;
        };
    };
};
