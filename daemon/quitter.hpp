/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asio.hpp"

#include <spdlog/spdlog.h>

#include <functional>

namespace partake::daemon {

template <typename AsioContext> class quitter {
    asio::signal_set signals;
    std::function<void()> notify;

  public:
    explicit quitter(AsioContext &asio_context,
                     std::function<void()> notify_quit) noexcept
        : signals(asio_context,
#ifdef _WIN32
                  SIGINT, SIGBREAK, SIGTERM
#else
                  SIGINT, SIGHUP, SIGTERM
#endif
                  ),
          notify(std::move(notify_quit)) {
    }

    void start() noexcept {
        signals.async_wait(
            [this](boost::system::error_code const &err, int sig) noexcept {
                if (not err) {
                    spdlog::info("signal {} received", sig);
                    notify();
                }
            });
    }

    void stop() noexcept {
        boost::system::error_code err;
        signals.cancel(err);
        if (err)
            spdlog::error("failed to cancel signal handler: {} ({})",
                          err.message(), err.value());
    }
};

} // namespace partake::daemon
