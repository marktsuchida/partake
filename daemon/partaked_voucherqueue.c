/*
 * Partake voucher expiration queue
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partaked_prefix.h"

#include "partaked_voucherqueue.h"

#include "partaked_malloc.h"
#include "partaked_object.h"
#include "partaked_pool.h"

#include "partake_proquint.h"

#include <zf_log.h>

#include <assert.h>


struct partaked_voucherqueue {
    uv_loop_t *event_loop;

    struct partaked_pool *pool;

    // Milliseconds after which a voucher expires.
    uint64_t time_to_live;

    // Extra milliseconds to wait past the expiry of the oldest voucher (to
    // batch together expirations).
    uint64_t timer_delay;

    uv_timer_t expiry_timer; // data set to pointer to this queue

    // Doubly-linked list, sorted by expiration, of vouchers.
    // dlist_head->next points to the oldest voucher; dlist_head->prev points
    // to the newest voucher. Other fields of the head are not used.
    // Probably a chuncked array-based deque is more efficient for large numbers
    // of vouchers.
    struct partaked_object dlist_head;
};


struct partaked_voucherqueue *partaked_voucherqueue_create(
        uv_loop_t *event_loop, struct partaked_pool *pool) {
    struct partaked_voucherqueue *ret = partaked_malloc(sizeof(*ret));
    ret->event_loop = event_loop;
    ret->pool = pool;
    ret->time_to_live = 60 * 1000; // Consider making configurable
    ret->timer_delay = 5 * 1000; // Consider making configurable
    int err = uv_timer_init(event_loop, &ret->expiry_timer);
    if (err != 0) {
        ZF_LOGE("uv_timer_init: %s", uv_strerror(err));
        partaked_free(ret);
        return NULL;
    }
    uv_handle_set_data((uv_handle_t *)&ret->expiry_timer, ret);
    ret->dlist_head.next = &ret->dlist_head;
    ret->dlist_head.prev = &ret->dlist_head;
    return ret;
}


// Expire a voucher that has been removed from the queue.
static void expire_voucher(struct partaked_voucherqueue *queue,
        struct partaked_object *voucher, const char *reason) {
    // TODO Print voucher and target tokens in proquint
    char voucher_token_pq[24];
    partake_proquint_from_uint64(voucher->token, voucher_token_pq);
    char target_token_pq[24];
    partake_proquint_from_uint64(voucher->target->token, target_token_pq);
    ZF_LOGW("voucher expired (%s): %s (for object %s)", reason,
            voucher_token_pq, target_token_pq);
    --voucher->target->refcount;
    partaked_pool_destroy_object(queue->pool, voucher);
}


void partaked_voucherqueue_destroy(struct partaked_voucherqueue *queue) {
    if (!queue)
        return;

    int err = uv_timer_stop(&queue->expiry_timer);
    if (err != 0)
        ZF_LOGE("uv_timer_stop: %s", uv_strerror(err));

    while (queue->dlist_head.next != &queue->dlist_head) {
        struct partaked_object *removed = queue->dlist_head.next;
        queue->dlist_head.next = removed->next;
        removed->next->prev = &queue->dlist_head;
        expire_voucher(queue, removed, "shutdown");
    }

    partaked_free(queue);
}


static void schedule_timer(struct partaked_voucherqueue *queue);


static void timer_callback(uv_timer_t *timer) {
    struct partaked_voucherqueue *queue =
            uv_handle_get_data((uv_handle_t *)timer);
    uint64_t now = uv_now(queue->event_loop);
    while (queue->dlist_head.next != &queue->dlist_head) {
        if (queue->dlist_head.next->expiration > now)
            break;
        struct partaked_object *removed = queue->dlist_head.next;
        queue->dlist_head.next = removed->next;
        removed->next->prev = &queue->dlist_head;
        expire_voucher(queue, removed, "timeout");
    }
    schedule_timer(queue);
}


static void schedule_timer(struct partaked_voucherqueue *queue) {
    if (queue->dlist_head.next == &queue->dlist_head) { // empty
        int err = uv_timer_stop(&queue->expiry_timer);
        if (err != 0)
            ZF_LOGE("uv_timer_stop: %s", uv_strerror(err));
    }
    else {
        uint64_t next_expiry = queue->dlist_head.next->expiration;
        uint64_t next_timer_time = next_expiry + queue->timer_delay;
        uint64_t now = uv_now(queue->event_loop);
        uint64_t timeout = next_timer_time > now ? next_timer_time - now : 0;
        int err = uv_timer_start(&queue->expiry_timer, timer_callback,
                timeout, 0);
        if (err != 0)
            ZF_LOGE("uv_timer_start: %s", uv_strerror(err));
    }
}


void partaked_voucherqueue_enqueue(struct partaked_voucherqueue *queue,
        struct partaked_object *voucher) {
    assert (voucher->next == NULL && voucher->prev == NULL);

    voucher->expiration = uv_now(queue->event_loop) + queue->time_to_live;

    struct partaked_object *p = queue->dlist_head.prev;
    voucher->next = &queue->dlist_head;
    voucher->prev = p;
    p->next = voucher;

    if (p == &queue->dlist_head) { // Was empty and inserted at front of queue
        schedule_timer(queue);
    }
}


void partaked_voucherqueue_remove(struct partaked_voucherqueue *queue,
        struct partaked_object *voucher) {
    struct partaked_object *n = voucher->next;
    struct partaked_object *p = voucher->prev;
    assert (n && p);

    n->prev = p;
    p->next = n;
    voucher->next = voucher->prev = NULL;

    if (p == &queue->dlist_head) { // Removed from front of queue
        schedule_timer(queue);
    }
}
