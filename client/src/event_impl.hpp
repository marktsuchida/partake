/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "partake/connection.hpp"
#include "partake/event.hpp"
#include "partake/objview.hpp"
#include "partake/token.hpp"
#include "partake/types.hpp"

#include <memory>
#include <system_error>

namespace partake::client::internal {

struct event_payload {
    op_id id = 0;
    op_type type = op_type::none;
    std::error_code error;
    void *user_data = nullptr;
    completion cont;     // One-shot; exchanged to empty by deliver().
    connection conn;     // connect
    objview obj;         // alloc, open, share, unshare
    token key;           // create_voucher, unshare
    bool zeroed = false; // alloc, unshare
};

[[nodiscard]] auto make_event(event_payload payload) -> event;

} // namespace partake::client::internal
