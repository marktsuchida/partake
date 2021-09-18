/*
 * Per-channel object handles
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <uthash.h>

// Per-channel object handle.
struct partaked_handle {
    struct partaked_object *object; // Owning pointer (reference counted)

    unsigned refcount;   // References held by client + pending requests
    unsigned open_count; // Alloc/Open minus Release/Unshare by client
    // The only case where refcount > open_count (between requests) is when
    // Open requests are pending on an unshared object.

    // All object handles are kept in a per-channel hash table.
    UT_hash_handle hh; // Key == object->token

    // Handles waiting for the object to be shared are kept in a
    // singly-linked list, whose head is pointed to by struct partaked_object.
    struct partaked_handle *next_waiting_for_share;

    struct partaked_handle_continuation *continuations_on_share; // slist

    struct partaked_handle_continuation *continuation_on_sole_ownership;
};

// Continuation function is called when the awaited condition is satisfied, or
// the premise for the wait no longer holds (e.g. the wait was for an object to
// be shared, but the object was released by its writer without ever being
// shared). It is also called, with handle == NULL, when the continuation
// itself is being canceled; in this latter case, the function should simply
// release whatever is pointed to by data.
typedef void (*partaked_handle_continuation_func)(
    struct partaked_handle *handle, void *data);

// Continuation function for on_share must check if the object is indeed
// shared when called, and act accordingly.
void partaked_handle_register_continue_on_share(
    struct partaked_handle *handle, void *registration_key,
    partaked_handle_continuation_func func, void *data);

void partaked_handle_cancel_continue_on_share(struct partaked_handle *handle,
                                              void *registration_key);

void partaked_handle_cancel_all_continue_on_share(
    struct partaked_handle *handle);

void partaked_handle_fire_on_share(struct partaked_object *object);

void partaked_handle_local_fire_on_share(struct partaked_handle *handle);

// Continuation function for on_sole_ownership must check if the handle and
// object open_count fields are both exactly 1, and act accordingly.
void partaked_handle_register_continue_on_sole_ownership(
    struct partaked_handle *handle, void *registration_key,
    partaked_handle_continuation_func func, void *data);

void partaked_handle_cancel_continue_on_sole_ownership(
    struct partaked_handle *handle, void *registration_key);

void partaked_handle_cancel_any_continue_on_sole_ownership(
    struct partaked_handle *handle);

void partaked_handle_fire_on_sole_ownership(struct partaked_object *object);

void partaked_handle_local_fire_on_sole_ownership(
    struct partaked_handle *handle);
