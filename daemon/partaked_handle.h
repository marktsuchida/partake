/*
 * Per-channel object handles
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
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
