// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::infra {

template <class Index>
concept ArrayIndex = std::is_trivially_copyable_v<Index>
    && std::is_aggregate_v<Index>
    && requires(Index i) { requires std::is_integral_v<decltype(i.v)>; }
    && sizeof(Index) == sizeof(decltype(std::declval<Index&>().v));

template <class T, std::size_t N, ArrayIndex Index>
class IndexedArray {
    TASTY_SEAT_EXEMPT(component);
    static_assert(N > 0, "an array no index can reach");

public:
    using value_type = T;
    using index_type = Index;

    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }

    constexpr T& operator[](Index i) noexcept { return elems_[static_cast<std::size_t>(i.v)]; }

    constexpr const T& operator[](Index i) const noexcept {
        return elems_[static_cast<std::size_t>(i.v)];
    }

    [[nodiscard]] constexpr OptRef<T> find(Index i) noexcept {
        if (static_cast<std::size_t>(i.v) >= N)
            return {};
        return OptRef<T>{elems_[static_cast<std::size_t>(i.v)]};
    }

    [[nodiscard]] constexpr OptRef<const T> find(Index i) const noexcept {
        if (static_cast<std::size_t>(i.v) >= N)
            return {};
        return OptRef<const T>{elems_[static_cast<std::size_t>(i.v)]};
    }

    constexpr auto begin() noexcept { return elems_.begin(); }

    constexpr auto end() noexcept { return elems_.end(); }

    constexpr auto begin() const noexcept { return elems_.begin(); }

    constexpr auto end() const noexcept { return elems_.end(); }

private:
    std::array<T, N> elems_{};
};

template <class Array, ArrayIndex Index>
    requires requires(Array& a) {
        a.data();
        a.size();
    }
[[nodiscard]] constexpr auto&& at_index(Array&& a, Index i) noexcept {
    return a[static_cast<std::size_t>(i.v)];
}

}
