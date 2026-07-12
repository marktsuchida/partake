/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "partake/event.hpp"

#include "event_impl.hpp"
#include "partake/connection.hpp"
#include "partake/objview.hpp"
#include "partake/token.hpp"
#include "partake/types.hpp"

#include <memory>
#include <system_error>
#include <utility>

namespace partake::client {

event::event(std::shared_ptr<internal::event_payload> p) noexcept
    : pl(std::move(p)) {}

auto event::id() const noexcept -> op_id { return pl ? pl->id : 0; }

auto event::type() const noexcept -> op_type {
    return pl ? pl->type : op_type::none;
}

auto event::error() const noexcept -> std::error_code {
    return pl ? pl->error : std::error_code();
}

auto event::user_data() const noexcept -> void * {
    return pl ? pl->user_data : nullptr;
}

auto event::get_connection() const -> connection {
    return pl ? pl->conn : connection();
}

auto event::object() const -> objview { return pl ? pl->obj : objview(); }

auto event::key() const noexcept -> token { return pl ? pl->key : token(); }

auto event::zeroed() const noexcept -> bool {
    return pl != nullptr and pl->zeroed;
}

auto event::deliver() -> bool {
    if (not pl or not pl->cont)
        return false;
    auto f = std::exchange(pl->cont, {});
    f(event(*this)); // Pass a copy; continuation may drop it freely.
    return true;
}

namespace internal {

auto make_event(std::shared_ptr<event_payload> payload) noexcept -> event {
    return event(std::move(payload));
}

auto make_event(event_payload payload) -> event {
    return make_event(std::make_shared<event_payload>(std::move(payload)));
}

} // namespace internal

} // namespace partake::client
