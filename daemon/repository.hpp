/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "hive.hpp"
#include "partake_protocol_generated.h"
#include "time_point.hpp"
#include "token.hpp"
#include "token_hash_table.hpp"

#include <gsl/pointers>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace partake::daemon {

template <typename Object, typename TokenSequence, typename VoucherQueue>
class repository {
  public:
    using object_type = Object;
    using token_sequence_type = TokenSequence;
    using voucher_queue_type = VoucherQueue;
    static_assert(
        std::is_same_v<object_type, typename voucher_queue_type::object_type>);

  private:
    // Objects are owned by 'object_storage' but also participate in the
    // hash table 'objects'. These ownerships do not keep the object alive;
    // instead, objects are held elsewhere by shared_ptr with a custom
    // deleter that removes the object from the hash table and storage.
    // User is responsible for releasing all objects before destroying the
    // repository.

    hive<object_type> object_storage;
    token_hash_table<object_type> objects;
    token_sequence_type tokseq;
    gsl::not_null<voucher_queue_type *> vqueue;

  public:
    explicit repository(token_sequence_type &&token_sequence,
                        voucher_queue_type &voucher_queue) noexcept
        : objects(1 << 3),
          tokseq(std::forward<token_sequence_type>(token_sequence)),
          vqueue(&voucher_queue) {}

    // No move or copy (empty state not defined)
    auto operator=(repository &&) = delete;

    template <typename R>
    auto create_object(protocol::Policy policy, R &&resource) noexcept
        -> std::shared_ptr<object_type> {
        auto obj = object_storage.emplace(tokseq.generate(), policy,
                                          std::forward<R>(resource));
        objects.insert(*obj);

        return {&*obj, [this](object_type *o) noexcept {
                    objects.erase(objects.iterator_to(*o));
                    object_storage.erase(object_storage.get_iterator(o));
                }};
    }

    // May return a voucher!
    auto find_object(token key) noexcept -> std::shared_ptr<object_type> {
        auto objit = objects.find(key);
        if (objit == objects.end())
            return {};
        return objit->shared_from_this();
    }

    void rekey_object(std::shared_ptr<object_type> const &obj) noexcept {
        assert(obj->is_proper_object());
        objects.erase(objects.iterator_to(*obj));
        obj->rekey(tokseq.generate());
        objects.insert(*obj);
    }

    auto create_voucher(std::shared_ptr<object_type> target,
                        time_point expiration, unsigned count) noexcept
        -> std::shared_ptr<object_type> {
        assert(target);
        assert(target->is_proper_object());
        assert(count > 0);
        target->as_proper_object().add_voucher();
        auto voucher = object_storage.emplace(
            tokseq.generate(), std::move(target), count, expiration);
        objects.insert(*voucher);
        auto ptr = std::shared_ptr<object_type>(
            &*voucher, [this](object_type *vchr) noexcept {
                vchr->as_voucher().target()->as_proper_object().drop_voucher();
                objects.erase(objects.iterator_to(*vchr));
                object_storage.erase(object_storage.get_iterator(vchr));
            });
        vqueue->enqueue(ptr);
        return ptr;
    }

    auto claim_voucher(std::shared_ptr<object_type> const &voucher,
                       time_point now) noexcept -> bool {
        assert(voucher);
        assert(voucher->is_voucher());
        auto &v = voucher->as_voucher();
        if (not v.claim(now))
            return false;
        if (not v.is_valid(now))
            vqueue->drop(voucher);
        return true;
    }

    void drop_all_vouchers() noexcept { vqueue->drop_all(); }

    void perform_housekeeping() noexcept {
        objects.rehash_if_appropriate(true);
    }
};

} // namespace partake::daemon
