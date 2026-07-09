/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "win32.hpp"

#ifdef _WIN32

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace partake::common::win32 {

auto strerror(unsigned err) -> std::string {
    std::string ret;
    char *msg = nullptr;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPSTR>(&msg), 0, nullptr) != 0) {
        ret = msg;
        LocalFree(reinterpret_cast<HLOCAL>(msg));
    } else {
        ret = fmt::format("Unknown error {}", err);
    }
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    return ret;
}

auto win32_handle::close() -> bool {
    if (h == invalid_handle())
        return true;
    bool ret = false;
    if (CloseHandle(h) == 0) {
        auto err = GetLastError();
        auto msg = strerror(err);
        lgr->error("CloseHandle: {}: {} ({})", h, msg, err);
    } else {
        lgr->info("CloseHandle: {}: success", h);
        ret = true;
    }
    h = invalid_handle();
    return ret;
}

unlinkable::unlinkable(std::string_view name,
                       std::shared_ptr<spdlog::logger> logger)
    : unlinkable(name, DeleteFileA, "DeleteFile", std::move(logger)) {}

auto unlinkable::unlink() -> bool {
    if (nm.empty())
        return true;
    bool ret = false;
    if (unlink_fn(nm.c_str()) == 0) {
        auto err = GetLastError();
        auto msg = strerror(err);
        lgr->error("{}: {}: {} ({})", fn_name, nm, msg, err);
    } else {
        lgr->info("{}: {}: success", fn_name, nm);
        ret = true;
    }
    nm.clear();
    return ret;
}

} // namespace partake::common::win32

#endif // _WIN32
