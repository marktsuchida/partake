/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "handle_list.hpp"
#include "hive.hpp"
#include "token.hpp"
#include "token_hash_table.hpp"

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace partake::daemon {

// A handle is owned by a session and contains per-session data about an
// object. The object is either open via the handle or awaiting to be opened.
template <typename Object>
class handle : public token_hash_table<handle<Object>>::hook,
               public handle_list<handle<Object>>::hook,
               public std::enable_shared_from_this<handle<Object>> {
  public:
    using object_type = Object; // Can be incomplete type.

  private:
    std::shared_ptr<object_type> obj;

    // Number of times opened by the session owning this handle
    unsigned open_count = 0;

    // Hold strong reference to self while open_count > 0
    std::shared_ptr<handle> shared_self;

    struct pending_request {
        std::shared_ptr<handle> self; // Retain reference
        std::function<void(std::shared_ptr<handle>)> handler;
    };
    hive<pending_request> requests_pending_on_share;
    std::optional<pending_request> request_pending_on_unique_ownership;

  public:
    explicit handle(std::shared_ptr<object_type> object)
        : obj(std::move(object)) {
        assert(obj->is_proper_object());
    }

    ~handle() {
        // We must only arrive here due to all shared_ptrs being destroyed,
        // thus triggering removal from the session. So the following must be
        // true.
        assert(requests_pending_on_share.empty());
        assert(not request_pending_on_unique_ownership.has_value());
        assert(open_count == 0);
    }

    // No move or copy (used with intrusive data structures and shared_ptr)
    auto operator=(handle &&) = delete;

    [[nodiscard]] auto key() const noexcept -> common::token {
        return obj->key();
    }

    auto object() noexcept -> std::shared_ptr<object_type> { return obj; }

    void open() noexcept {
        if (open_count == 0) {
            assert(not shared_self);
            shared_self = this->shared_from_this();
            obj->as_proper_object().open();
        }
        ++open_count;
    }

    void close() noexcept {
        assert(open_count > 0);
        --open_count;
        if (open_count == 0) {
            obj->as_proper_object().close(this);
            assert(shared_self);
            shared_self.reset();
        } else if (is_open_uniquely() &&
                   obj->as_proper_object()
                       .has_handle_awaiting_unique_ownership()) {
            obj->as_proper_object().clear_handle_awaiting_unique_ownership(
                this);
            resume_request_pending_on_unique_ownership();
        }
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_count > 0;
    }

    [[nodiscard]] auto is_open_uniquely() const noexcept -> bool {
        return open_count == 1 &&
               obj->as_proper_object().is_opened_by_unique_handle();
    }

    void add_request_pending_on_share(
        std::function<void(std::shared_ptr<handle>)> handler) noexcept {
        obj->as_proper_object().add_handle_awaiting_share(this);
        requests_pending_on_share.insert(
            {this->shared_from_this(), std::move(handler)});
    }

    void set_request_pending_on_unique_ownership(
        std::function<void(std::shared_ptr<handle>)> handler) noexcept {
        assert(not request_pending_on_unique_ownership.has_value());
        obj->as_proper_object().set_handle_awaiting_unique_ownership(this);
        request_pending_on_unique_ownership = {this->shared_from_this(),
                                               std::move(handler)};
    }

    void resume_requests_pending_on_share() noexcept {
        auto keep_me = this->shared_from_this();
        for (auto &pending : requests_pending_on_share)
            pending.handler(std::move(pending.self));
        requests_pending_on_share.clear();
    }

    void resume_request_pending_on_unique_ownership() noexcept {
        if (request_pending_on_unique_ownership.has_value()) {
            auto &pending = request_pending_on_unique_ownership.value();
            pending.handler(std::move(pending.self));
            request_pending_on_unique_ownership.reset();
        }
    }

    void drop_pending_requests() noexcept {
        auto keep_me = this->shared_from_this();
        if (not requests_pending_on_share.empty()) {
            obj->as_proper_object().remove_handle_awaiting_share(this);
            requests_pending_on_share.clear();
        }
        if (request_pending_on_unique_ownership) {
            obj->as_proper_object().clear_handle_awaiting_unique_ownership(
                this);
            request_pending_on_unique_ownership.reset();
        }
    }

    void close_all() noexcept {
        drop_pending_requests();
        while (open_count > 0)
            close();
    }
};

} // namespace partake::daemon
