// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <utility>
#include <type_traits>

namespace mister::infra {

template <class T>
concept VShape = std::is_aggregate_v<T> && std::is_trivially_copyable_v<T> && requires(T t) {
    requires std::is_integral_v<decltype(t.v)>;
} && sizeof(T) == sizeof(std::remove_cvref_t<decltype(std::declval<T&>().v)>);

namespace affine {

template <class T>
using rep_t = std::remove_cvref_t<decltype(std::declval<T&>().v)>;

template <class R>
concept AffineRep = std::is_integral_v<R>
    && !std::same_as<R, bool>
    && !std::same_as<R, char>
    && !std::same_as<R, wchar_t>
    && !std::same_as<R, char8_t>
    && !std::same_as<R, char16_t>
    && !std::same_as<R, char32_t>;

template <class T>
concept Affine = VShape<T> && requires { typename T::affine_count; } && AffineRep<rep_t<T>>;

template <class Index, class Count>
concept CountOf = requires { typename Index::affine_count; }
    && std::same_as<Count, typename Index::affine_count>;

template <class C>
concept Count = Affine<C> && std::same_as<typename C::affine_count, C>;

template <class P>
concept Position = Affine<P>
    && !std::same_as<typename P::affine_count, P>
    && Count<typename P::affine_count>
    && std::same_as<rep_t<P>, rep_t<typename P::affine_count>>;

template <class P>
concept Index = Position<P> && std::same_as<typename P::affine_index, P>;

template <Position P>
using count_t = typename P::affine_count;

void overflow_in_constant_expression();

template <class R>
using wide_t = std::
    conditional_t<std::is_unsigned_v<R> && (sizeof(R) < sizeof(unsigned)), unsigned, R>;

template <class R>
constexpr R add(R a, R b) noexcept {
    if consteval {
        R out{};
        if (__builtin_add_overflow(a, b, &out))
            overflow_in_constant_expression();
        return out;
    }
    return static_cast<R>(static_cast<wide_t<R>>(a) + static_cast<wide_t<R>>(b));
}

template <class R>
constexpr R sub(R a, R b) noexcept {
    if consteval {
        R out{};
        if (__builtin_sub_overflow(a, b, &out))
            overflow_in_constant_expression();
        return out;
    }
    return static_cast<R>(static_cast<wide_t<R>>(a) - static_cast<wide_t<R>>(b));
}

template <class R>
constexpr R mul(R a, R b) noexcept {
    if consteval {
        R out{};
        if (__builtin_mul_overflow(a, b, &out))
            overflow_in_constant_expression();
        return out;
    }
    return static_cast<R>(static_cast<wide_t<R>>(a) * static_cast<wide_t<R>>(b));
}

template <std::unsigned_integral R, std::unsigned_integral K>
    requires(sizeof(K) <= sizeof(R))
[[nodiscard]] constexpr R div_ceil(R n, K unit) noexcept {
    const R d = static_cast<R>(unit);
    return static_cast<R>(n / d + static_cast<R>(n % d != 0));
}

template <Count C>
[[nodiscard]] constexpr C operator+(C a, C b) noexcept {
    return C{add(a.v, b.v)};
}

template <Count C>
[[nodiscard]] constexpr C operator-(C a, C b) noexcept {
    return C{sub(a.v, b.v)};
}

template <Count C, std::same_as<rep_t<C>> K>
[[nodiscard]] constexpr C operator*(C a, K k) noexcept {
    return C{mul(a.v, k)};
}

template <Count C, std::same_as<rep_t<C>> K>
[[nodiscard]] constexpr C operator*(K k, C a) noexcept {
    return a * k;
}

template <Count C>
constexpr C& operator+=(C& a, C b) noexcept {
    return a = a + b;
}

template <Count C>
constexpr C& operator-=(C& a, C b) noexcept {
    return a = a - b;
}

template <Count C, std::same_as<rep_t<C>> K>
[[nodiscard]] constexpr C operator/(C a, K k) noexcept {
    return C{static_cast<rep_t<C>>(a.v / k)};
}

template <Count C>
[[nodiscard]] constexpr rep_t<C> operator/(C a, C b) noexcept {
    return static_cast<rep_t<C>>(a.v / b.v);
}

template <Position P>
[[nodiscard]] constexpr P operator+(P p, count_t<P> n) noexcept {
    return P{add(p.v, n.v)};
}

template <Position P>
[[nodiscard]] constexpr P operator+(count_t<P> n, P p) noexcept {
    return p + n;
}

template <Position P>
[[nodiscard]] constexpr P operator-(P p, count_t<P> n) noexcept {
    return P{sub(p.v, n.v)};
}

template <Position P>
[[nodiscard]] constexpr count_t<P> operator-(P a, P b) noexcept {
    return count_t<P>{sub(a.v, b.v)};
}

template <Position P>
constexpr P& operator+=(P& p, count_t<P> n) noexcept {
    return p = p + n;
}

template <Position P>
constexpr P& operator-=(P& p, count_t<P> n) noexcept {
    return p = p - n;
}

template <Count C>
[[nodiscard]] constexpr C saturating_sub(C a, C b) noexcept {
    return a < b ? C{} : C{sub(a.v, b.v)};
}

template <Count C>
[[nodiscard]] constexpr std::optional<C> checked_sub(C a, C b) noexcept {
    if (a < b)
        return std::nullopt;
    return C{sub(a.v, b.v)};
}

template <Position P>
[[nodiscard]] constexpr count_t<P> distance(P a, P b) noexcept {
    return a < b ? count_t<P>{sub(b.v, a.v)} : count_t<P>{sub(a.v, b.v)};
}

template <Count C>
[[nodiscard]] constexpr C rounded_up_to(C a, C multiple) noexcept {
    return C{static_cast<rep_t<C>>(div_ceil(a.v, multiple.v) * multiple.v)};
}

template <Position P>
[[nodiscard]] constexpr bool fits(P at, count_t<P> len, count_t<P> bound) noexcept {
    return len.v <= bound.v && at.v <= bound.v - len.v;
}

template <Index P>
[[nodiscard]] constexpr bool in_bounds(count_t<P> n, P i) noexcept {
    return i.v < n.v;
}

template <Index P>
[[nodiscard]] constexpr P end_of(count_t<P> n) noexcept {
    return P{n.v};
}

}

using affine::operator+;
using affine::operator-;
using affine::operator*;
using affine::operator/;
using affine::operator+=;
using affine::operator-=;

}
