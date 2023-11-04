/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "time_point.hpp"
#include "voucher_queue.hpp"

#include <cassert>
#include <memory>
#include <optional>

namespace partake::daemon {

template <typename Object> class voucher {
  public:
    using object_type = Object;
    using queue_type = voucher_queue<object_type>;

  private:
    std::shared_ptr<object_type> tgt;
    unsigned ct; // Only decremented after construction
    time_point expiry;

    std::optional<typename queue_type::handle_type> handle_in_queue;

  public:
    explicit voucher(std::shared_ptr<object_type> target, unsigned count,
                     time_point expiration)
        : tgt(std::move(target)), ct(count), expiry(expiration) {}

    ~voucher() { assert(not handle_in_queue.has_value()); }

    // No move or copy (empty state not defined)
    auto operator=(voucher &&) = delete;

    [[nodiscard]] auto target() const noexcept
        -> std::shared_ptr<object_type> {
        return tgt;
    }

    [[nodiscard]] auto remaining_count() const noexcept -> unsigned {
        return ct;
    }

    [[nodiscard]] auto expiration() const noexcept -> time_point {
        return expiry;
    }

    [[nodiscard]] auto is_valid(time_point now) const noexcept -> bool {
        return ct > 0 && expiry >= now;
    }

    auto claim(time_point now) noexcept -> bool {
        if (not is_valid(now))
            return false;
        --ct;
        return true;
    }

    // TODO: Instead of clear_queued(), use an RAII object that can be placed
    // in the queue and auto-clears on destruction (still need function to get
    // handle-in-queue)

    void set_queued(typename queue_type::handle_type handle) {
        assert(not handle_in_queue.has_value());
        handle_in_queue = handle;
    }

    auto clear_queued() -> std::optional<typename queue_type::handle_type> {
        auto ret = handle_in_queue;
        handle_in_queue.reset();
        return ret;
    }
};

} // namespace partake::daemon
