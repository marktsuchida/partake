/*
 * Per-channel object handles
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

#include <uthash.h>


// Per-channel object handle.
struct partake_handle {
    struct partake_object *object; // Owning pointer (reference counted)

    unsigned refcount; // References held by client + pending requests
    unsigned open_count; // Alloc/Acquire minus Release/Unpublish by client
    // The only case where refcount > open_count (between requests) is when
    // Acquire requests are pending on an unpublished object.

    // All object handles are kept in a per-channel hash table.
    UT_hash_handle hh; // Key == object->token

    // Handles waiting for the object to be published are kept in a
    // singly-linked list, whose head is pointed to by struct partake_object.
    struct partake_handle *next_waiting_for_publish;

    struct partake_handle_continuation *continuations_on_publish; // slist

    struct partake_handle_continuation *continuation_on_sole_ownership;
};


// Continuation function is called when the awaited condition is satisfied, or
// the premise for the wait no longer holds (e.g. the wait was for an object to
// be published, but the object was released by its writer without ever being
// published). It is also called, with handle == NULL, when the continuation
// itself is being canceled; in this latter case, the function should simply
// release whatever is pointed to by data.
typedef void (*partake_handle_continuation_func)(
        struct partake_handle *handle, void *data);


// Continuation function for on_publish must check if the object is indeed
// published when called, and act accordingly.
void partake_handle_register_continue_on_publish(
        struct partake_handle *handle, void *registration_key,
        partake_handle_continuation_func func, void *data);

void partake_handle_cancel_continue_on_publish(
        struct partake_handle *handle, void *registration_key);

void partake_handle_cancel_all_continue_on_publish(
        struct partake_handle *handle);

void partake_handle_fire_on_publish(struct partake_object *object);

void partake_handle_local_fire_on_publish(struct partake_handle *handle);


// Continuation function for on_sole_ownership must check if the handle and
// object open_count fields are both exactly 1, and act accordingly.
void partake_handle_register_continue_on_sole_ownership(
        struct partake_handle *handle, void *registration_key,
        partake_handle_continuation_func func, void *data);

void partake_handle_cancel_continue_on_sole_ownership(
        struct partake_handle *handle, void *registration_key);

void partake_handle_cancel_any_continue_on_sole_ownership(
        struct partake_handle *handle);

void partake_handle_fire_on_sole_ownership(struct partake_object *object);

void partake_handle_local_fire_on_sole_ownership(struct partake_handle *handle);
