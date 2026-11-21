// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

namespace mister::infra::serial {

template <class L>
using rep_t = std::remove_cvref_t<decltype(std::declval<const L&>().v)>;

template <class R>
concept Rep = std::is_integral_v<R>
    && std::is_unsigned_v<R>
    && !std::same_as<R, bool>
    && !std::same_as<R, char>
    && !std::same_as<R, char8_t>
    && !std::same_as<R, char16_t>
    && !std::same_as<R, char32_t>
    && !std::same_as<R, wchar_t>;

using Skip = std::true_type;
using Through = std::false_type;

template <class R, R Max, class Zero>
struct Rule {
    using rep = R;
    using zero = Zero;
    static constexpr R kMax = Max;
};

template <class R, R Max = std::numeric_limits<R>::max()>
using SkipZero = Rule<R, Max, Skip>;
template <class R, R Max = std::numeric_limits<R>::max()>
using ThroughZero = Rule<R, Max, Through>;

template <class L>
concept Label =
    requires {
        typename L::serial_rule;
        typename L::serial_rule::rep;
    }
    && requires(const L& x) { x.v; }
    && std::is_aggregate_v<L>
    && std::is_trivially_copyable_v<L>
    && Rep<typename L::serial_rule::rep>
    && std::same_as<rep_t<L>, typename L::serial_rule::rep>
    && sizeof(L) == sizeof(rep_t<L>);

using Order = std::true_type;

template <class L>
concept Ordered = Label<L> && requires {
    typename L::serial_order;
} && std::same_as<typename L::serial_order, Order>;

template <Label L>
[[nodiscard]] constexpr L next(L x) noexcept {
    using R = rep_t<L>;
    constexpr R kMax = L::serial_rule::kMax;
    constexpr R kWrapped = std::same_as<typename L::serial_rule::zero, Skip> ? R{1} : R{0};

    return L{x.v >= kMax ? kWrapped : static_cast<R>(x.v + R{1})};
}

template <Ordered L>
[[nodiscard]] constexpr bool ahead(L x, rep_t<L> earlier) noexcept {
    using R = rep_t<L>;
    constexpr R kMax = L::serial_rule::kMax;
    if constexpr (kMax == std::numeric_limits<R>::max()) {
        using S = std::make_signed_t<R>;
        return static_cast<S>(x.v - earlier) > S{0};
    } else {
        constexpr R kHalf = static_cast<R>((kMax >> 1u) + R{1});
        const R delta = static_cast<R>((x.v - earlier) & kMax);
        return delta != R{0} && delta < kHalf;
    }
}

template <Ordered L>
[[nodiscard]] constexpr bool at_or_past(L x, rep_t<L> earlier) noexcept {
    using R = rep_t<L>;
    constexpr R kMax = L::serial_rule::kMax;
    if constexpr (kMax == std::numeric_limits<R>::max()) {
        using S = std::make_signed_t<R>;
        return static_cast<S>(x.v - earlier) >= S{0};
    } else {
        constexpr R kHalf = static_cast<R>((kMax >> 1u) + R{1});
        return static_cast<R>((x.v - earlier) & kMax) < kHalf;
    }
}

}
