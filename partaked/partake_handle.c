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

#include "prefix.h"

#include "partake_handle.h"
#include "partake_malloc.h"
#include "partake_object.h"

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
