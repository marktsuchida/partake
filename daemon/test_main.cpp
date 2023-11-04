/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <boost/stacktrace.hpp>
#include <trompeloeil.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

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

    trompeloeil::set_reporter([](trompeloeil::severity s, char const *file,
                                 unsigned long line, std::string const &msg) {
        auto const *f = line != 0u ? file : "[file/line unavailable]";
        if (s == trompeloeil::severity::fatal)
            ADD_FAIL_AT(f, static_cast<int>(line), msg);
        else
            ADD_FAIL_CHECK_AT(f, static_cast<int>(line), msg);
    });

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
