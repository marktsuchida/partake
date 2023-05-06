/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <boost/intrusive/list.hpp>

namespace partake::daemon {

namespace intru = boost::intrusive;

template <typename Handle> class handle_list {
    struct list_tag;

  public:
    using handle_type = Handle;
    using hook = typename intru::list_base_hook<intru::tag<list_tag>>;

  private:
    using list_impl_type = intru::list<handle_type, intru::base_hook<hook>>;

    list_impl_type list;

  public:
    // For now, only non-const member functions are wrapped, since we don't use
    // const ones anywhere.

    using iterator = typename list_impl_type::iterator;

    auto begin() noexcept -> iterator { return list.begin(); }
    auto end() noexcept -> iterator { return list.end(); }

    [[nodiscard]] auto empty() const noexcept -> bool { return list.empty(); }
    void clear() noexcept { list.clear(); }

    [[nodiscard]] auto iterator_to(handle_type &h) noexcept -> iterator {
        return list.iterator_to(h);
    }

    [[nodiscard]] auto front() noexcept -> handle_type & {
        return list.front();
    }

    void push_back(handle_type &h) noexcept { list.push_back(h); }

    void pop_front() noexcept { list.pop_front(); }

    auto erase(iterator it) noexcept -> iterator { return list.erase(it); }
};

} // namespace partake::daemon
