/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace partake::common {

[[nodiscard]] inline auto null_logger() -> std::shared_ptr<spdlog::logger> {
    static auto lgr =
        spdlog::create<spdlog::sinks::null_sink_mt>("null_logger");
    return lgr;
}

} // namespace partake::common
