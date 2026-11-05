// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstdint>
#include <utility>
#include <type_traits>

namespace mister::infra::affine {

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
concept Affine = std::is_aggregate_v<T> && std::is_trivially_copyable_v<T> && requires {
    typename T::affine_count;
} && AffineRep<rep_t<T>> && sizeof(T) == sizeof(rep_t<T>);

template <class C>
concept Count = Affine<C> && std::same_as<typename C::affine_count, C>;

template <class P>
concept Position = Affine<P>
    && !std::same_as<typename P::affine_count, P>
    && Count<typename P::affine_count>
    && std::same_as<rep_t<P>, rep_t<typename P::affine_count>>;

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

}
