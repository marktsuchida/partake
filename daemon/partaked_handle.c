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


struct partake_handle_continuation {
    struct partake_handle_continuation *next; // utlist, singly-linked
    void *key; // Identifier for cancellation
    partake_handle_continuation_func func;
    void *data;
};


void partake_handle_register_continue_on_publish(
        struct partake_handle *handle, void *registration_key,
        partake_handle_continuation_func func, void *data) {
    if (handle->continuations_on_publish == NULL) {
        LL_PREPEND2(handle->object->handles_waiting_for_publish, handle,
                next_waiting_for_publish);
    }

    struct partake_handle_continuation *cont = partake_malloc(sizeof(*cont));
    cont->key = registration_key;
    cont->func = func;
    cont->data = data;

    LL_PREPEND(handle->continuations_on_publish, cont);
}


void partake_handle_cancel_continue_on_publish(struct partake_handle *handle,
        void *registration_key) {
    struct partake_handle_continuation *cont = NULL;
    LL_SEARCH_SCALAR(handle->continuations_on_publish, cont, key,
            registration_key);
    assert (cont != NULL);

    LL_DELETE(handle->continuations_on_publish, cont);

    if (handle->continuations_on_publish == NULL) {
        LL_DELETE2(handle->object->handles_waiting_for_publish, handle,
                next_waiting_for_publish);
    }

    cont->func(NULL, cont->data); // Release data
    partake_free(cont);
}


static void do_handle_on_publish(struct partake_handle *handle, bool cancel) {
    LL_DELETE2(handle->object->handles_waiting_for_publish, handle,
            next_waiting_for_publish);
    handle->next_waiting_for_publish = NULL;

    struct partake_handle_continuation *cont, *tmpc;
    LL_FOREACH_SAFE(handle->continuations_on_publish, cont, tmpc) {
        LL_DELETE(handle->continuations_on_publish, cont);

        cont->func(cancel ? NULL : handle, cont->data);
        partake_free(cont);
    }
}


void partake_handle_cancel_all_continue_on_publish(
        struct partake_handle *handle) {
    do_handle_on_publish(handle, true);
}


void partake_handle_fire_on_publish(struct partake_object *object) {
    struct partake_handle *handle, *tmph;
    LL_FOREACH_SAFE2(object->handles_waiting_for_publish, handle, tmph,
            next_waiting_for_publish) {
        do_handle_on_publish(handle, false);
    }
}


void partake_handle_local_fire_on_publish(struct partake_handle *handle) {
    do_handle_on_publish(handle, false);
}


void partake_handle_register_continue_on_sole_ownership(
        struct partake_handle *handle, void *registration_key,
        partake_handle_continuation_func func, void *data) {
    if (handle->continuation_on_sole_ownership == NULL) {
        assert (handle->object->handle_waiting_for_sole_ownership == NULL);
        handle->object->handle_waiting_for_sole_ownership = handle;
    }

    struct partake_handle_continuation *cont = partake_malloc(sizeof(*cont));
    cont->key = registration_key;
    cont->func = func;
    cont->data = data;

    handle->continuation_on_sole_ownership = cont;
}


void partake_handle_cancel_continue_on_sole_ownership(
        struct partake_handle *handle, void *registration_key) {
    assert (handle->continuation_on_sole_ownership->key == registration_key);
    struct partake_handle_continuation *cont =
        handle->continuation_on_sole_ownership;
    handle->continuation_on_sole_ownership = NULL;

    assert (handle->object->handle_waiting_for_sole_ownership == handle);
    handle->object->handle_waiting_for_sole_ownership = NULL;

    cont->func(NULL, cont->data); // Release data
    partake_free(cont);
}


void partake_handle_cancel_any_continue_on_sole_ownership(
        struct partake_handle *handle) {
    struct partake_handle_continuation *cont =
        handle->continuation_on_sole_ownership;
    if (cont == NULL)
        return;

    handle->continuation_on_sole_ownership = NULL;

    assert (handle->object->handle_waiting_for_sole_ownership == handle);
    handle->object->handle_waiting_for_sole_ownership = NULL;

    cont->func(NULL, cont->data); // Release data
    partake_free(cont);
}


void partake_handle_fire_on_sole_ownership(struct partake_object *object) {
    struct partake_handle *handle = object->handle_waiting_for_sole_ownership;
    if (handle == NULL)
        return;
    assert (handle->continuation_on_sole_ownership != NULL);
    object->handle_waiting_for_sole_ownership = NULL;

    struct partake_handle_continuation *cont =
        handle->continuation_on_sole_ownership;
    handle->continuation_on_sole_ownership = NULL;

    cont->func(handle, cont->data);
    partake_free(cont);
}


void partake_handle_local_fire_on_sole_ownership(
        struct partake_handle *handle) {
    struct partake_handle_continuation *cont =
        handle->continuation_on_sole_ownership;
    if (cont == NULL)
        return;

    handle->continuation_on_sole_ownership = NULL;

    assert (handle->object->handle_waiting_for_sole_ownership == handle);
    handle->object->handle_waiting_for_sole_ownership = NULL;

    cont->func(handle, cont->data);
    partake_free(cont);
}
