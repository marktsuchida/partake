/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "asio.hpp"
#include "cli.hpp"
#include "daemon.hpp"

auto main(int argc, char const *const argv[]) -> int {
    using namespace partake::daemon;
    auto const result =
        parse_cli_args(argc, argv)
            .and_then([](daemon_config const &cfg) -> tl::expected<void, int> {
                asio::io_context ioctx(1);
                auto daemon = partake_daemon(ioctx, cfg);
                daemon.start();
                ioctx.run();
                auto status = daemon.exit_code();
                if (status != 0)
                    return tl::unexpected(status);
                return {};
            });
    return result.has_value() ? 0 : result.error();
}
