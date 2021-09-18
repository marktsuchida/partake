/*
 * Per-channel object handles
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_handle.h"
#include "partaked_malloc.h"
#include "partaked_object.h"

#include <utlist.h>

#include <stdbool.h>

struct partaked_handle_continuation {
    struct partaked_handle_continuation *next; // utlist, singly-linked
    void *key;                                 // Identifier for cancellation
    partaked_handle_continuation_func func;
    void *data;
};

void partaked_handle_register_continue_on_share(
    struct partaked_handle *handle, void *registration_key,
    partaked_handle_continuation_func func, void *data) {
    if (handle->continuations_on_share == NULL) {
        LL_PREPEND2(handle->object->handles_waiting_for_share, handle,
                    next_waiting_for_share);
    }

    struct partaked_handle_continuation *cont = partaked_malloc(sizeof(*cont));
    cont->key = registration_key;
    cont->func = func;
    cont->data = data;

    LL_PREPEND(handle->continuations_on_share, cont);
}

void partaked_handle_cancel_continue_on_share(struct partaked_handle *handle,
                                              void *registration_key) {
    struct partaked_handle_continuation *cont = NULL;
    LL_SEARCH_SCALAR(handle->continuations_on_share, cont, key,
                     registration_key);
    assert(cont != NULL);

    LL_DELETE(handle->continuations_on_share, cont);

    if (handle->continuations_on_share == NULL) {
        LL_DELETE2(handle->object->handles_waiting_for_share, handle,
                   next_waiting_for_share);
    }

    cont->func(NULL, cont->data); // Release data
    partaked_free(cont);
}

static void do_handle_on_share(struct partaked_handle *handle, bool cancel) {
    LL_DELETE2(handle->object->handles_waiting_for_share, handle,
               next_waiting_for_share);
    handle->next_waiting_for_share = NULL;

    struct partaked_handle_continuation *cont, *tmpc;
    LL_FOREACH_SAFE(handle->continuations_on_share, cont, tmpc) {
        LL_DELETE(handle->continuations_on_share, cont);

        cont->func(cancel ? NULL : handle, cont->data);
        partaked_free(cont);
    }
}

void partaked_handle_cancel_all_continue_on_share(
    struct partaked_handle *handle) {
    do_handle_on_share(handle, true);
}

void partaked_handle_fire_on_share(struct partaked_object *object) {
    struct partaked_handle *handle, *tmph;
    LL_FOREACH_SAFE2(object->handles_waiting_for_share, handle, tmph,
                     next_waiting_for_share) {
        do_handle_on_share(handle, false);
    }
}

void partaked_handle_local_fire_on_share(struct partaked_handle *handle) {
    do_handle_on_share(handle, false);
}

void partaked_handle_register_continue_on_sole_ownership(
    struct partaked_handle *handle, void *registration_key,
    partaked_handle_continuation_func func, void *data) {
    if (handle->continuation_on_sole_ownership == NULL) {
        assert(handle->object->handle_waiting_for_sole_ownership == NULL);
        handle->object->handle_waiting_for_sole_ownership = handle;
    }

    struct partaked_handle_continuation *cont = partaked_malloc(sizeof(*cont));
    cont->key = registration_key;
    cont->func = func;
    cont->data = data;

    handle->continuation_on_sole_ownership = cont;
}

void partaked_handle_cancel_continue_on_sole_ownership(
    struct partaked_handle *handle, void *registration_key) {
    assert(handle->continuation_on_sole_ownership->key == registration_key);
    struct partaked_handle_continuation *cont =
        handle->continuation_on_sole_ownership;
    handle->continuation_on_sole_ownership = NULL;

    assert(handle->object->handle_waiting_for_sole_ownership == handle);
    handle->object->handle_waiting_for_sole_ownership = NULL;

    cont->func(NULL, cont->data); // Release data
    partaked_free(cont);
}

void partaked_handle_cancel_any_continue_on_sole_ownership(
    struct partaked_handle *handle) {
    struct partaked_handle_continuation *cont =
        handle->continuation_on_sole_ownership;
    if (cont == NULL)
        return;

    handle->continuation_on_sole_ownership = NULL;

    assert(handle->object->handle_waiting_for_sole_ownership == handle);
    handle->object->handle_waiting_for_sole_ownership = NULL;

    cont->func(NULL, cont->data); // Release data
    partaked_free(cont);
}

void partaked_handle_fire_on_sole_ownership(struct partaked_object *object) {
    struct partaked_handle *handle = object->handle_waiting_for_sole_ownership;
    if (handle == NULL)
        return;
    assert(handle->continuation_on_sole_ownership != NULL);
    object->handle_waiting_for_sole_ownership = NULL;

    struct partaked_handle_continuation *cont =
        handle->continuation_on_sole_ownership;
    handle->continuation_on_sole_ownership = NULL;

    cont->func(handle, cont->data);
    partaked_free(cont);
}

void partaked_handle_local_fire_on_sole_ownership(
    struct partaked_handle *handle) {
    struct partaked_handle_continuation *cont =
        handle->continuation_on_sole_ownership;
    if (cont == NULL)
        return;

    handle->continuation_on_sole_ownership = NULL;

    assert(handle->object->handle_waiting_for_sole_ownership == handle);
    handle->object->handle_waiting_for_sole_ownership = NULL;

    cont->func(handle, cont->data);
    partaked_free(cont);
}
