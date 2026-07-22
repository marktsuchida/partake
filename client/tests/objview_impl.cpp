/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "objview_impl.hpp"

#include "client_impl.hpp"
#include "connection_impl.hpp"
#include "mapping.hpp"
#include "partake/objview.hpp"
#include "partake/token.hpp"
#include "queue_impl.hpp"
#include "shmem_attach.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

// Unit scope is modest (a connection_impl is required for construction, and
// close submission needs live I/O); the close/auto-close behavior is covered
// by the integration tests in objview.cpp.

namespace partake::client::internal {

namespace {

// A mapping over a plain buffer: a default-constructed posix_mmap_region is
// a no-op backing.
auto make_fake_mapping(void *base, std::size_t size)
    -> std::shared_ptr<mapping> {
    return std::make_shared<mapping>(base, size, posix_mmap_region());
}

} // namespace

TEST_CASE("objview_impl: accessors, close transitions, handle access pair") {
    auto conn = std::make_shared<connection_impl>(
        std::make_shared<client_impl>(), std::make_shared<queue_impl>());
    std::array<unsigned char, 64> buf{};
    auto ov = std::make_shared<objview_impl>(
        conn, make_fake_mapping(buf.data(), 64), 16, 32, true, token(123));

    CHECK(ov->key() == token(123));
    CHECK(ov->size() == 32);
    CHECK(ov->writable());
    CHECK(ov->data() == &buf[16]);

    auto const handle = make_objview(ov);
    CHECK(handle);
    CHECK(get_objview_impl(handle) == ov);

    CHECK(ov->begin_close());       // open -> closing.
    CHECK(ov->data() == nullptr);   // No longer open.
    CHECK_FALSE(ov->begin_close()); // Lost: not open anymore.
    ov->mark_closed();
    CHECK(ov->data() == nullptr);
    CHECK_FALSE(ov->begin_close());
    // st == closed, so destruction does not submit a close.
}

} // namespace partake::client::internal
