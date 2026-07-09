/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include <catch2/catch_session.hpp>

#include <boost/stacktrace.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

void print_stacktrace() {
    try {
        std::cerr << boost::stacktrace::stacktrace();
    } catch (...) {
    }
    std::abort();
}

} // namespace

auto main(int argc, char const *const *argv) -> int {
    std::set_terminate(&print_stacktrace);
    return Catch::Session().run(argc, argv);
}
