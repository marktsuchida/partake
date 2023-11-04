/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "handle.hpp"
#include "partake_protocol_generated.h"
#include "proper_object.hpp"
#include "time_point.hpp"
#include "token.hpp"
#include "token_hash_table.hpp"
#include "voucher.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace partake::daemon {

using object_policy = protocol::Policy;

template <typename Resource>
class object : public token_hash_table<object<Resource>>::hook,
               public std::enable_shared_from_this<object<Resource>> {
  public:
    using resource_type = Resource;
    using handle_type = handle<object<resource_type>>;
    using proper_object_type = proper_object<resource_type, handle_type>;
    using voucher_type = voucher<object<resource_type>>;

  private:
    common::token ky;
    object_policy pol;
    std::variant<proper_object_type, voucher_type> body;

  public:
    explicit object(common::token key, object_policy policy,
                    resource_type &&mem) noexcept
        : ky(key), pol(policy), body(std::in_place_type<proper_object_type>,
                                     std::forward<resource_type>(mem)) {}

    explicit object(common::token key, std::shared_ptr<object> target,
                    unsigned count, time_point expiration) noexcept
        : ky(key), pol(target->policy()),
          body(std::in_place_type<voucher_type>, std::move(target), count,
               expiration) {}

    // No move or copy (used with intrusive data structures and shared_ptr)
    auto operator=(object &&) = delete;

    [[nodiscard]] auto key() const noexcept -> common::token { return ky; }

    // Must not be called when the object is in an repository or has a handle
    // in a session.
    void rekey(common::token key) noexcept { ky = key; }

    [[nodiscard]] auto policy() const noexcept -> object_policy { return pol; }

    [[nodiscard]] auto is_proper_object() const noexcept -> bool {
        return std::holds_alternative<proper_object_type>(body);
    }

    [[nodiscard]] auto is_voucher() const noexcept -> bool {
        return std::holds_alternative<voucher_type>(body);
    }

    [[nodiscard]] auto as_proper_object() noexcept -> proper_object_type & {
        return std::get<proper_object_type>(body);
    }

    [[nodiscard]] auto as_voucher() noexcept -> voucher_type & {
        return std::get<voucher_type>(body);
    }
};

} // namespace partake::daemon
