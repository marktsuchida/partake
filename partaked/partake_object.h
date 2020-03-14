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

struct partake_channel;


/*
 * Objects start their life unpublished. They may get published once. Objects
 * are reference counted, and they are deallocated when the count reaches zero.
 * Unpublished objects are not sharable, but their reference count may become
 * greater than 1 if others are waiting for publication.  Unpublishing is
 * equivalent to deallocating and allocating a new object, except that the old
 * object's buffer is reused; therefore it can only be performed when the
 * reference count is 1.
 *
 * Alternatively, objects can be allocated with the 'share mutable' flag set.
 * In this case the object is immediately sharable and cannot be published or
 * unpublished. Acquiring the object returns a writable reference in this case.
 */


// Object flags
enum {
    PARTAKE_OBJECT_PUBLISHED = 1 << 0,
    PARTAKE_OBJECT_SHARE_MUTABLE = 1 << 1,
};


// Object descriptor
struct partake_object {
    partake_token token;
    /* unsigned segment; */ // Currently always 0
    size_t offset; // Relative to segment base address
    size_t size;
    short flags;

    // The reference count is the number of _channels_ holding a reference to
    // this object. This includes channels waiting for this object to change
    // state. (Note that a single channel can hold multiple references to an
    // object; the object doesn't know about this.)
    unsigned refcount;

    // The channel currently holding a writable reference. Always NULL for
    // published or share-mutable objects.
    struct partake_channel *exclusive_writer;

    // Object descriptors are kept in a global hash table for their entire
    // lifetime. This reference is _not_ included in the refcount field above.
    UT_hash_handle hh; // Key == token
};
