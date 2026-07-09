/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "posix.hpp"

#ifndef _WIN32

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <cstdlib> // mkstemp
#include <cstring> // strerror_r

#include <unistd.h>

namespace partake::common::posix {

auto strerror(int errn) -> std::string {
    // See Linux man strerror_r(3) regarding POSIX vs GNU variants. We assign
    // the result to a specific type so as to catch any misconfiguration.
    std::string ret;
    ret.resize(512);
#if _GNU_SOURCE || (__linux__ && (_POSIX_C_SOURCE < 200112L))
    char *msg = ::strerror_r(errn, ret.data(), ret.size());
    return msg;
#else
    int const failed = ::strerror_r(errn, ret.data(), ret.size());
    if (failed != 0)
        return fmt::format("Unknown error {}", errn);
    ret.resize(std::strlen(ret.data()));
    return ret;
#endif
}

auto file_descriptor::close() -> bool {
    if (fd == invalid_fd)
        return true;
    bool ret = false;
    errno = 0;
    if (::close(fd) != 0) {
        auto err = errno;
        auto msg = strerror(err);
        lgr->error("close: fd {}: {} ({})", fd, msg, err);
    } else {
        lgr->info("close: fd {}: success", fd);
        ret = true;
    }
    fd = invalid_fd;
    return ret;
}

unlinkable::unlinkable(std::string_view name,
                       std::shared_ptr<spdlog::logger> logger)
    : unlinkable(name, ::unlink, "unlink", std::move(logger)) {}

} // namespace partake::common::posix

#endif // _WIN32
