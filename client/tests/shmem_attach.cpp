/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "shmem_attach.hpp"

#include "partake/errors.hpp"

#include "mapping.hpp"
#include "requests.hpp"
#include "shmem_mmap.hpp"
#include "shmem_sysv.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

namespace partake::client::internal {

namespace {

constexpr std::size_t page = 16384;

// Prove the client mapping and the daemon segment name the same physical
// region: writes through one are visible through the other.
void check_shared(mapping const &m, void *daemon_addr, std::size_t size) {
    REQUIRE(m.base() != nullptr);
    REQUIRE(m.size() == size);
    auto *cli = static_cast<volatile std::uint8_t *>(m.base());
    auto *dmn = static_cast<volatile std::uint8_t *>(daemon_addr);
    cli[0] = 0xab;
    cli[size - 1] = 0xcd;
    CHECK(dmn[0] == 0xab);
    CHECK(dmn[size - 1] == 0xcd);
    dmn[1] = 0xef;
    CHECK(cli[1] == 0xef);
}

} // namespace

TEST_CASE("shmem_attach: posix shm_open round trip") {
    auto seg = daemon::create_posix_mmap_shmem(page);
    REQUIRE(seg.is_valid());

    auto result =
        attach(segment_spec{seg.size(), posix_mmap_spec{seg.name(), true}});
    REQUIRE(result.has_value());
    check_shared(**result, seg.address(), seg.size());
}

TEST_CASE("shmem_attach: posix file-backed round trip") {
    auto seg = daemon::create_file_mmap_shmem(page);
    REQUIRE(seg.is_valid());

    auto result =
        attach(segment_spec{seg.size(), posix_mmap_spec{seg.name(), false}});
    REQUIRE(result.has_value());
    check_shared(**result, seg.address(), seg.size());
}

TEST_CASE("shmem_attach: sysv round trip") {
    // Runs a real IPC_PRIVATE round trip (must be run outside the Claude Code
    // sandbox); skips only if segment creation genuinely fails.
    auto seg = daemon::create_sysv_shmem(page);
    if (not seg.is_valid()) {
        SUCCEED("SysV shared memory unavailable");
        return;
    }

    auto result = attach(segment_spec{seg.size(), sysv_shmem_spec{seg.id()}});
    REQUIRE(result.has_value());
    check_shared(**result, seg.address(), seg.size());
}

TEST_CASE("shmem_attach: bogus posix name fails") {
    auto result = attach(segment_spec{
        page, posix_mmap_spec{"/partake-nonexistent-shmem-xyz", true}});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error()); // A nonzero (system-category) error.
}

TEST_CASE("shmem_attach: win32 spec is unsupported") {
    auto result = attach(
        segment_spec{page, win32_mapping_spec{"Local\\Whatever", false}});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == make_error_code(client_errc::protocol_violation));
}

} // namespace partake::client::internal
