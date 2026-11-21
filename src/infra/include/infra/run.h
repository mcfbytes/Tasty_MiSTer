// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/affine.h"
#include "infra/span_cut.h"

namespace mister::infra::affine {

template <Position P>
struct Run {
    P start{};
    count_t<P> len{};

    [[nodiscard]] constexpr P end() const noexcept { return start + len; }

    [[nodiscard]] constexpr bool contains(P at) const noexcept {
        return at >= start && (at - start) < len;
    }

    [[nodiscard]] constexpr bool fits_in(count_t<P> bound) const noexcept {
        return affine::fits(start, len, bound);
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> slice_of(std::span<T> s) const noexcept {
        return affine::first(affine::from(s, start), len);
    }

    template <class T, std::size_t N>
    [[nodiscard]] constexpr std::span<T> slice_of(std::span<T, N> s) const noexcept {
        return slice_of(std::span<T>{s});
    }

    friend constexpr bool operator==(Run, Run) = default;
};

}
