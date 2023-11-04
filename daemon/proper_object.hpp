/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "handle_list.hpp"

#include <cassert>
#include <cstddef>
#include <utility>

namespace partake::daemon {

template <typename Resource, typename Handle> class proper_object {
  public:
    using resource_type = Resource;
    using handle_type = Handle;

  private:
    bool shared = false;         // Always false for PRIMITIVE policy
    unsigned n_open_handles = 0; // Not including handles waiting to open
    unsigned n_vouchers = 0;
    resource_type rsrc;

    // The following are null/empty for PRIMITIVE policy. For DEFAULT policy,
    // non-null pointers are guaranteed to be valid because sessions and
    // handles deregister themselves before ending lifetime.
    handle_type *exc_writer = nullptr;
    handle_list<handle_type> handles_awaiting_share;
    handle_type *handle_awaiting_unique_ownership = nullptr;

  public:
    explicit proper_object(resource_type &&resource) noexcept
        : rsrc(std::forward<resource_type>(resource)) {}

    ~proper_object() {
        assert(n_open_handles == 0);
        assert(n_vouchers == 0);
        assert(exc_writer == nullptr);
        assert(handles_awaiting_share.empty());
        assert(handle_awaiting_unique_ownership == nullptr);
    }

    // No move or copy (empty state not defined)
    auto operator=(proper_object &&) = delete;

    [[nodiscard]] auto resource() const noexcept -> resource_type const & {
        return rsrc;
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return n_open_handles > 0;
    }

    [[nodiscard]] auto is_opened_by_unique_handle() const noexcept -> bool {
        return n_open_handles == 1 && n_vouchers == 0;
    }

    [[nodiscard]] auto is_shared() const noexcept -> bool { return shared; }

    void exclusive_writer(handle_type *hnd) noexcept {
        assert(exc_writer == nullptr);
        assert(hnd != nullptr);
        exc_writer = hnd;
    }

    [[nodiscard]] auto exclusive_writer() const noexcept -> handle_type * {
        return exc_writer;
    }

    void open() noexcept { ++n_open_handles; }

    void close(handle_type *hnd) noexcept {
        assert(hnd != nullptr);
        assert(n_open_handles > 0);
        --n_open_handles;

        // Resume pending Unshare requests if now uniquely opened. Also resume
        // (so that it can fail) if closed by the handle that was awaiting
        // unique ownership.
        auto &h_awaiting = handle_awaiting_unique_ownership;
        if ((h_awaiting != nullptr && n_open_handles == 1 && n_vouchers == 0 &&
             h_awaiting->is_open_uniquely()) ||
            h_awaiting == hnd) {
            h_awaiting->resume_request_pending_on_unique_ownership();
            h_awaiting = nullptr;
        }

        // Resume pending Open requests (so that they can fail) if all handles
        // were closed (namely, the exclusive writer closed).
        if (exc_writer == hnd) {
            exc_writer = nullptr;
            assert(n_open_handles == 0); // By definition of exclusive.

            while (not handles_awaiting_share.empty()) {
                // Must remove from list first, because resumption may destroy
                // the handle.
                auto &h = handles_awaiting_share.front();
                handles_awaiting_share.pop_front();
                h.resume_requests_pending_on_share();
            }
        }
    }

    void share() noexcept {
        assert(not shared);
        assert(exc_writer != nullptr);
        shared = true;
        exc_writer = nullptr;

        for (auto &h_awaiting : handles_awaiting_share)
            h_awaiting.resume_requests_pending_on_share();
        handles_awaiting_share.clear();
    }

    void unshare(handle_type *new_exclusive_writer) noexcept {
        assert(new_exclusive_writer != nullptr);
        assert(shared);
        assert(n_open_handles == 1);
        assert(exc_writer == nullptr);
        shared = false;
        exc_writer = new_exclusive_writer;
    }

    void add_handle_awaiting_share(handle_type *hnd) noexcept {
        assert(not shared);
        handles_awaiting_share.push_back(*hnd);
    }

    void remove_handle_awaiting_share(handle_type *hnd) noexcept {
        handles_awaiting_share.erase(handles_awaiting_share.iterator_to(*hnd));
    }

    void set_handle_awaiting_unique_ownership(handle_type *hnd) noexcept {
        assert(shared);
        assert(handle_awaiting_unique_ownership == nullptr);
        handle_awaiting_unique_ownership = hnd;
    }

    auto has_handle_awaiting_unique_ownership() const noexcept {
        return handle_awaiting_unique_ownership != nullptr;
    }

    void clear_handle_awaiting_unique_ownership(handle_type *hnd) noexcept {
        assert(handle_awaiting_unique_ownership == hnd);
        handle_awaiting_unique_ownership = nullptr;
    }

    void add_voucher() noexcept { ++n_vouchers; }

    void drop_voucher() noexcept {
        assert(n_vouchers > 0);
        --n_vouchers;

        // Resume pending Unshare requests if now uniquely opened.
        auto &h_awaiting = handle_awaiting_unique_ownership;
        if ((h_awaiting != nullptr && n_open_handles == 1 && n_vouchers == 0 &&
             h_awaiting->is_open_uniquely())) {
            h_awaiting->resume_request_pending_on_unique_ownership();
            h_awaiting = nullptr;
        }
    }
};

} // namespace partake::daemon
