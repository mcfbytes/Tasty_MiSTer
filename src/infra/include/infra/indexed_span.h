// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::infra {

template <class T, ArrayIndex Index>
class IndexedSpan {
    TASTY_SEAT_EXEMPT(component);

public:
    using value_type = T;
    using index_type = Index;
    using count_type = typename Index::affine_count;

    constexpr IndexedSpan() noexcept = default;

    constexpr explicit IndexedSpan(std::span<T> s) noexcept : s_(s) {}

    [[nodiscard]] constexpr count_type size() const noexcept {
        return count_type{static_cast<decltype(count_type::v)>(s_.size())};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return s_.empty(); }

    [[nodiscard]] constexpr T& operator[](Index i) const noexcept {
        return s_[static_cast<std::size_t>(i.v)];
    }

    [[nodiscard]] constexpr OptRef<T> find(Index i) const noexcept {
        if (static_cast<std::size_t>(i.v) >= s_.size())
            return {};
        return OptRef<T>{s_[static_cast<std::size_t>(i.v)]};
    }

    constexpr auto begin() const noexcept { return s_.begin(); }

    constexpr auto end() const noexcept { return s_.end(); }

private:
    std::span<T> s_{};
};

}
