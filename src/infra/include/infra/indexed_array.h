// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

#include "infra/affine.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/span_cut.h"

namespace mister::infra {

template <class Index>
concept ArrayIndex = VShape<Index>;

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

    template <class Count>
        requires affine::CountOf<Index, Count>
    [[nodiscard]] constexpr std::span<T> prefix(Count count) noexcept {
        return affine::first(std::span<T>(elems_), count);
    }

    template <class Count>
        requires affine::CountOf<Index, Count>
    [[nodiscard]] constexpr std::span<const T> prefix(Count count) const noexcept {
        return affine::first(std::span<const T>(elems_), count);
    }

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
