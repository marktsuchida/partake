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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace partake::daemon {

template <typename Allocator, typename Repository, typename Handle,
          typename Segment>
class session {
  public:
    using allocator_type = Allocator;
    using repository_type = Repository;
    using object_type = typename repository_type::object_type;
    using handle_type = Handle;
    using segment_type = Segment;
    static_assert(
        std::is_same_v<typename object_type::resource_type,
                       decltype(std::declval<allocator_type>().allocate(0))>);
    static_assert(
        std::is_same_v<typename handle_type::object_type, object_type>);

  private:
    segment_type const *seg = nullptr;
    allocator_type *allocr = nullptr;
    repository_type *repo = nullptr;

    hive<handle_type> handle_storage;
    token_hash_table<handle_type> handles;

    bool valid = false;
    bool has_said_hello = false;
    std::string client_name;
    std::uint32_t client_pid = 0;
    std::uint32_t id = 0;

    std::chrono::milliseconds voucher_ttl = std::chrono::seconds(10);

  public:
    // Construct in empty state, on which the only valid operations are
    // destruction, move-assignment, and swap.
    session() noexcept = default;

    explicit session(std::uint32_t session_id, segment_type const &segment,
                     allocator_type &allocator, repository_type &repository,
                     std::chrono::milliseconds voucher_time_to_live) noexcept
        : seg(&segment), allocr(&allocator), repo(&repository),
          handles(1 << 3), valid(true), id(session_id),
          voucher_ttl(voucher_time_to_live) {
        assert(seg != nullptr);
        assert(allocr != nullptr);
        assert(repo != nullptr);
    }

    ~session() { close_session(); }

    // Noncopyable

    session(session &&other) noexcept
        : seg(std::exchange(other.seg, nullptr)),
          allocr(std::exchange(other.allocr, nullptr)),
          repo(std::exchange(other.repo, nullptr)),
          handle_storage(std::move(other.handle_storage)),
          handles(std::move(other.handles)),
          valid(std::exchange(other.valid, false)),
          has_said_hello(other.has_said_hello),
          client_name(std::move(other.client_name)),
          client_pid(other.client_pid), id(other.id),
          voucher_ttl(other.voucher_ttl) {}

    auto operator=(session &&rhs) noexcept -> session & {
        close_session();
        session(std::move(rhs)).swap(*this);
        return *this;
    }

    void swap(session &other) noexcept {
        using std::swap;
        swap(seg, other.seg);
        swap(allocr, other.allocr);
        swap(repo, other.repo);
        swap(handle_storage, other.handle_storage);
        swap(handles, other.handles);
        swap(valid, other.valid);
        swap(has_said_hello, other.has_said_hello);
        swap(client_name, other.client_name);
        swap(client_pid, other.client_pid);
        swap(id, other.id);
        swap(voucher_ttl, other.voucher_ttl);
    }

    friend void swap(session &lhs, session &rhs) noexcept { lhs.swap(rhs); }

    [[nodiscard]] auto is_valid() const noexcept -> bool { return valid; }

    [[nodiscard]] auto session_id() const noexcept -> std::uint32_t {
        assert(valid);
        return id;
    }

    [[nodiscard]] auto name() const noexcept -> std::string {
        assert(valid);
        return client_name;
    }

    [[nodiscard]] auto pid() const noexcept -> std::uint32_t {
        assert(valid);
        return client_pid;
    }

    template <typename Success, typename Error>
    void hello(std::string_view name, std::uint32_t pid, Success success_cb,
               Error error_cb) noexcept {
        assert(valid);
        if (has_said_hello) {
            error_cb(protocol::Status::INVALID_REQUEST);
        } else {
            // Enforce 1023-byte limit on client name (TODO Make error?)
            client_name = name.substr(0, 1023);
            client_pid = pid;
            has_said_hello = true;
            success_cb(id);
        }
    }

    template <typename Success, typename Error>
    void get_segment(std::uint32_t segment_id, Success success_cb,
                     Error error_cb) noexcept {
        assert(valid);

        // For now there is only one segment, id 0.
        if (segment_id == 0) {
            success_cb(seg->spec());
        } else {
            error_cb(protocol::Status::NO_SUCH_SEGMENT);
        }
    }

    template <typename Success, typename Error>
    void alloc(std::uint64_t size, protocol::Policy policy, Success success_cb,
               Error error_cb) noexcept {
        assert(valid);

        if (size > std::numeric_limits<std::size_t>::max()) // 32-bit Systems
            return error_cb(protocol::Status::OUT_OF_SHMEM);
        auto s = static_cast<std::size_t>(size);
        auto obj = repo->create_object(policy, allocr->allocate(s));
        if (not obj)
            return error_cb(protocol::Status::OUT_OF_SHMEM);

        auto hnd = create_handle(obj);
        hnd->open();
        auto &po = obj->as_proper_object();
        if (policy == protocol::Policy::DEFAULT)
            po.exclusive_writer(hnd.get());
        // Segment currently hard-coded to 0.
        auto const &rsrc = po.resource();
        success_cb(obj->key(), rsrc);
    }

    template <typename ImmediateSuccess, typename ImmediateError,
              typename DeferredSuccess, typename DeferredError>
    void open(btoken key, protocol::Policy policy, bool wait, time_point now,
              ImmediateSuccess success_cb, ImmediateError error_cb,
              DeferredSuccess deferred_success_cb,
              DeferredError deferred_error_cb) noexcept {
        assert(valid);

        std::shared_ptr<object_type> obj;
        std::shared_ptr<object_type> vchr;
        auto hnd = find_handle(key);
        if (hnd)
            obj = hnd->object();
        else
            std::tie(obj, vchr) = find_target(key, now);
        if (not obj || obj->policy() != policy)
            return error_cb(protocol::Status::NO_SUCH_OBJECT);

        bool const can_open_immediately =
            obj->policy() == protocol::Policy::PRIMITIVE ||
            obj->as_proper_object().is_shared();

        if (not can_open_immediately) {
            // Edge case: object was closed before sharing (but lingers due to
            // a voucher); cannot be accessed.
            if (not obj->as_proper_object().is_open()) {
                if (vchr)
                    repo->claim_voucher(vchr, now);
                return error_cb(protocol::Status::NO_SUCH_OBJECT);
            }

            if (not wait)
                return error_cb(protocol::Status::OBJECT_BUSY);
        }

        if (vchr) {
            if (not repo->claim_voucher(vchr, now))
                return error_cb(protocol::Status::NO_SUCH_OBJECT);
        }

        if (not hnd)
            hnd = create_handle(obj);

        if (can_open_immediately) {
            hnd->open();
            auto const &rsrc = obj->as_proper_object().resource();
            return success_cb(obj->key(), rsrc);
        }

        hnd->add_request_pending_on_share(
            [deferred_success_cb, deferred_error_cb](
                std::shared_ptr<handle_type> const &handle) noexcept {
                auto o = handle->object();
                auto const &po = o->as_proper_object();
                if (po.is_shared()) {
                    handle->open();
                    auto const &rsrc = po.resource();
                    deferred_success_cb(o->key(), rsrc);
                } else {
                    deferred_error_cb(protocol::Status::NO_SUCH_OBJECT);
                }
            });
    }

    template <typename Success, typename Error>
    void close(btoken key, Success success_cb, Error error_cb) noexcept {
        assert(valid);
        auto hnd = find_handle(key);
        if (not hnd || not hnd->is_open())
            return error_cb(protocol::Status::NO_SUCH_OBJECT);
        hnd->close();
        success_cb();
    }

    template <typename Success, typename Error>
    void share(btoken key, Success success_cb, Error error_cb) noexcept {
        assert(valid);
        auto hnd = find_handle(key);
        if (not hnd ||
            hnd->object()->as_proper_object().exclusive_writer() != hnd.get())
            return error_cb(protocol::Status::NO_SUCH_OBJECT);
        hnd->object()->as_proper_object().share();
        success_cb();
    }

    template <typename ImmediateSuccess, typename ImmediateError,
              typename DeferredSuccess, typename DeferredError>
    void unshare(btoken key, bool wait, ImmediateSuccess success_cb,
                 ImmediateError error_cb, DeferredSuccess deferred_success_cb,
                 DeferredError deferred_error_cb) noexcept {
        assert(valid);

        auto hnd = find_handle(key);
        if (not hnd || not hnd->is_open())
            return error_cb(protocol::Status::NO_SUCH_OBJECT);
        auto obj = hnd->object();
        if (not obj->as_proper_object().is_shared())
            return error_cb(protocol::Status::NO_SUCH_OBJECT);

        if (obj->as_proper_object().has_handle_awaiting_unique_ownership())
            return error_cb(protocol::Status::OBJECT_RESERVED);

        bool const can_unshare_immediately = hnd->is_open_uniquely();
        if (not can_unshare_immediately && not wait)
            return error_cb(protocol::Status::OBJECT_BUSY);
        if (can_unshare_immediately)
            return success_cb(do_unshare(hnd));

        hnd->set_request_pending_on_unique_ownership(
            [deferred_success_cb, deferred_error_cb,
             this](std::shared_ptr<handle_type> const &handle) noexcept {
                if (handle->is_open_uniquely())
                    return deferred_success_cb(do_unshare(handle));
                return deferred_error_cb(protocol::Status::NO_SUCH_OBJECT);
            });
    }

    template <typename Success, typename Error>
    void create_voucher(btoken target, unsigned count, time_point now,
                        Success success_cb, Error error_cb) noexcept {
        assert(valid);

        if (count == 0)
            return error_cb(protocol::Status::INVALID_REQUEST);

        auto const hnd = find_handle(target);
        std::shared_ptr<object_type> real_target;
        if (hnd) {
            real_target = hnd->object();
        } else {
            std::shared_ptr<object_type> vchr;
            std::tie(real_target, vchr) = find_target(target, now);
            if (not real_target)
                return error_cb(protocol::Status::NO_SUCH_OBJECT);
        }

        auto expiration = now + voucher_ttl;
        auto voucher = repo->create_voucher(real_target, expiration, count);

        success_cb(voucher->key());
    }

    template <typename Success, typename Error>
    void discard_voucher(btoken key, time_point now, Success success_cb,
                         Error error_cb) noexcept {
        assert(valid);

        auto obj = repo->find_object(key);
        if (not obj)
            return error_cb(protocol::Status::NO_SUCH_OBJECT);
        if (obj->is_proper_object())
            return success_cb(key);

        auto target = obj->as_voucher().target()->key();
        if (repo->claim_voucher(obj, now))
            return success_cb(target);
        return error_cb(protocol::Status::NO_SUCH_OBJECT);
    }

    void drop_pending_requests() noexcept {
        assert(valid);
        // Iterate in a manner that allows item erasure.
        for (auto i = handles.begin(), e = handles.end(); i != e;) {
            auto n = std::next(i);
            i->drop_pending_requests();
            i = n;
        }
    }

    void perform_housekeeping() noexcept {
        assert(valid);
        handles.rehash_if_appropriate(true);
    }

  private:
    void close_session() noexcept {
        if (not valid)
            return;

        // Before starting closing handles, drop all of _our_ pending
        // requests so that they do not get resumed.
        drop_pending_requests();

        // Iterate in a manner that allows item erasure.
        for (auto i = handles.begin(), e = handles.end(); i != e;) {
            auto n = std::next(i);
            i->close_all();
            i = n;
        }

        assert(handles.empty());
        assert(handle_storage.empty());

        valid = false;
    }

    auto create_handle(std::shared_ptr<object_type> object) noexcept
        -> std::shared_ptr<handle_type> {
        auto hnd = handle_storage.emplace(std::move(object));
        handles.insert(*hnd);
        return {&*hnd, [this](handle_type *h) noexcept {
                    handles.erase(handles.iterator_to(*h));
                    handle_storage.erase(handle_storage.get_iterator(h));
                }};
    }

    auto find_handle(btoken key) noexcept -> std::shared_ptr<handle_type> {
        auto hnd = handles.find(key);
        if (hnd == handles.end())
            return {};
        return hnd->shared_from_this();
    }

    // Returns pair of shared_ptr<object>s: {target, voucher}.
    auto find_target(btoken key, time_point now) noexcept {
        auto obj = repo->find_object(key);
        std::shared_ptr<object_type> vchr;
        if (obj && obj->is_voucher()) {
            auto &v = obj->as_voucher();
            if (v.is_valid(now))
                vchr = std::exchange(obj, v.target());
            else
                obj.reset();
        }
        return std::pair{obj, vchr};
    }

    auto do_unshare(std::shared_ptr<handle_type> const &hnd) noexcept
        -> btoken {
        auto obj = hnd->object();

        // Temporarily remove from handle table while key changes.
        handles.erase(handles.iterator_to(*hnd));
        obj->as_proper_object().unshare(hnd.get());
        repo->rekey_object(obj);
        handles.insert(*hnd);

        return obj->key();
    }
};

} // namespace partake::daemon
