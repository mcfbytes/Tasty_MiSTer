// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ranges>

#include "infra/affine.h"

namespace mister::infra::affine {

namespace detail {

template <Index P>
struct AsIndex {
    [[nodiscard]] constexpr P operator()(rep_t<P> at) const noexcept { return P{at}; }
};

}

template <Index P>
[[nodiscard]] constexpr auto indices(count_t<P> n) noexcept {
    return std::views::iota(rep_t<P>{}, n.v) | std::views::transform(detail::AsIndex<P>{});
}

}
