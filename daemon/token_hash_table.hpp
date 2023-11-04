/*
 * This file is part of the partake project
 * Copyright 2020-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "token.hpp"

#include <boost/intrusive/unordered_set.hpp>

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace partake::daemon {

namespace intru = boost::intrusive;

// Wrapper around intrusive unordered set to hide some implementation details.
template <typename E> class token_hash_table {
  public:
    using hook = intru::unordered_set_base_hook<>;
    using element_type = E;
    // Element must inherit from hook, but we cannot test here because it will
    // be an incomplete type.

  private:
    struct key_getter {
        using type = common::token;
        auto operator()(element_type const &h) const -> type {
            return h.key();
        }
    };

    using table_impl_type =
        intru::unordered_set<element_type, intru::base_hook<hook>,
                             intru::hash<std::hash<common::token>>,
                             intru::key_of_value<key_getter>,
                             intru::power_2_buckets<true>>;

    std::vector<typename table_impl_type::bucket_type> buckets;
    table_impl_type table;

  public:
    explicit token_hash_table(std::size_t initial_buckets = 8)
        : buckets(initial_buckets),
          table(typename table_impl_type::bucket_traits(buckets.data(),
                                                        buckets.size())) {}

    // For now, only non-const member functions are wrapped, since we don't use
    // const ones anywhere.

    using iterator = typename table_impl_type::iterator;

    auto begin() noexcept -> iterator { return table.begin(); }
    auto end() noexcept -> iterator { return table.end(); }

    [[nodiscard]] auto empty() const noexcept -> bool { return table.empty(); }

    [[nodiscard]] auto iterator_to(element_type &e) noexcept -> iterator {
        return table.iterator_to(e);
    }

    void insert(element_type &e) noexcept { table.insert(e); }
    void erase(iterator it) noexcept { table.erase(it); }

    auto find(common::token key) noexcept -> iterator {
        return table.find(key);
    }

    // Iterators will be invalidated
    void rehash_if_appropriate(bool allow_shrink = true) noexcept {
        auto const current = table.bucket_count();
        auto const usage = table.size();

        // Load factor thresholds used here are tentative (not profiled).
        std::size_t new_count = current;
        if (usage > current / 2 * 3)
            new_count = 2 * current;
        else if (allow_shrink && usage < current / 8)
            new_count = std::max(std::size_t(8), current / 4);
        if (new_count == current)
            return;

        decltype(buckets) wk(new_count);
        table.rehash(
            typename table_impl_type::bucket_traits(wk.data(), wk.size()));
        buckets.swap(wk);
    }
};

} // namespace partake::daemon
