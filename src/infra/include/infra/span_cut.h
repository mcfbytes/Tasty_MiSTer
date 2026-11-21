// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

#include "infra/affine.h"

namespace mister::infra::affine {

template <class V>
concept CutIndex = std::is_aggregate_v<V>
    && std::is_trivially_copyable_v<V>
    && AffineRep<rep_t<V>>
    && sizeof(V) == sizeof(rep_t<V>);

template <class V, class T>
concept CutsElement = CutIndex<V>
    && (!requires { V::cut_element_bytes; } || sizeof(T) == V::cut_element_bytes);

template <class T, CutIndex C>
    requires CutsElement<C, T>
[[nodiscard]] constexpr std::span<T> first(std::span<T> s, C count) noexcept {
    return s.first(static_cast<std::size_t>(count.v));
}

template <class T, std::size_t N, CutIndex C>
    requires CutsElement<C, T>
[[nodiscard]] constexpr std::span<T> first(std::span<T, N> s, C count) noexcept {
    return first(std::span<T>{s}, count);
}

template <class T, CutIndex C>
    requires CutsElement<C, T>
[[nodiscard]] constexpr std::span<T> first_clamped(std::span<T> s, C count) noexcept {
    const std::size_t n = static_cast<std::size_t>(count.v);
    return s.first(n < s.size() ? n : s.size());
}

template <class T, std::size_t N, CutIndex C>
    requires CutsElement<C, T>
[[nodiscard]] constexpr std::span<T> first_clamped(std::span<T, N> s, C count) noexcept {
    return first_clamped(std::span<T>{s}, count);
}

template <class T, CutIndex P>
    requires CutsElement<P, T>
[[nodiscard]] constexpr std::span<T> from(std::span<T> s, P at) noexcept {
    return s.subspan(static_cast<std::size_t>(at.v));
}

template <class T, std::size_t N, CutIndex P>
    requires CutsElement<P, T>
[[nodiscard]] constexpr std::span<T> from(std::span<T, N> s, P at) noexcept {
    return from(std::span<T>{s}, at);
}

template <class T, CutIndex P>
    requires CutsElement<P, T>
[[nodiscard]] constexpr std::span<T> from_clamped(std::span<T> s, P at) noexcept {
    const std::size_t n = static_cast<std::size_t>(at.v);
    return s.subspan(n < s.size() ? n : s.size());
}

template <class T, std::size_t N, CutIndex P>
    requires CutsElement<P, T>
[[nodiscard]] constexpr std::span<T> from_clamped(std::span<T, N> s, P at) noexcept {
    return from_clamped(std::span<T>{s}, at);
}

template <CutIndex C>
    requires CutsElement<C, char>
[[nodiscard]] constexpr std::string_view first(std::string_view text, C count) noexcept {
    return text.substr(0, static_cast<std::size_t>(count.v));
}

template <CutIndex C>
    requires CutsElement<C, char>
[[nodiscard]] constexpr std::string_view first_clamped(std::string_view text, C count) noexcept {
    const std::size_t n = static_cast<std::size_t>(count.v);
    return text.substr(0, n < text.size() ? n : text.size());
}

template <CutIndex P>
    requires CutsElement<P, char>
[[nodiscard]] constexpr std::string_view from(std::string_view text, P at) noexcept {
    return text.substr(static_cast<std::size_t>(at.v));
}

template <CutIndex P>
    requires CutsElement<P, char>
[[nodiscard]] constexpr std::string_view from_clamped(std::string_view text, P at) noexcept {
    const std::size_t n = static_cast<std::size_t>(at.v);
    return text.substr(n < text.size() ? n : text.size());
}

}
