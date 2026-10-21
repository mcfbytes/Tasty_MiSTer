// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "infra/byte_view.h"

namespace mister::infra {

template <class T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] constexpr T load_native(std::span<const std::byte, sizeof(T)> b) noexcept {
    std::array<std::byte, sizeof(T)> raw;
    std::ranges::copy(b, raw.begin());
    return std::bit_cast<T>(raw);
}

template <class T, ByteLike B>
    requires(std::is_trivially_copyable_v<T> && !std::is_same_v<std::remove_cv_t<B>, std::byte>)
[[nodiscard]] constexpr T load_native(std::span<B, sizeof(T)> b) noexcept {
    return load_native<T>(byte_cast<const std::byte>(std::span<B, sizeof(T)>(b)));
}

template <class T>
    requires std::is_trivially_copyable_v<T>
constexpr void store_native(std::span<std::byte, sizeof(T)> out, const T& v) noexcept {
    const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(v);
    std::ranges::copy(raw, out.begin());
}

template <class T, ByteLike B>
    requires(
        std::is_trivially_copyable_v<T>
        && !std::is_same_v<std::remove_cv_t<B>, std::byte>
        && !std::is_const_v<B>
    )
constexpr void store_native(std::span<B, sizeof(T)> out, const T& v) noexcept {
    store_native<T>(byte_cast<std::byte>(out), v);
}

template <class T, ByteLike B>
    requires(std::integral<T> && !std::same_as<T, bool>)
[[nodiscard]] constexpr T load_le(std::span<B, sizeof(T)> b) noexcept {
    const T v = load_native<T>(b);
    if constexpr (std::endian::native == std::endian::little)
        return v;
    else
        return std::byteswap(v);
}

template <class T, ByteLike B>
    requires(std::integral<T> && !std::same_as<T, bool>)
[[nodiscard]] constexpr T load_be(std::span<B, sizeof(T)> b) noexcept {
    const T v = load_native<T>(b);
    if constexpr (std::endian::native == std::endian::big)
        return v;
    else
        return std::byteswap(v);
}

template <class T, ByteLike B>
    requires(std::integral<T> && !std::same_as<T, bool> && !std::is_const_v<B>)
constexpr void store_le(std::span<B, sizeof(T)> out, T v) noexcept {
    if constexpr (std::endian::native != std::endian::little)
        v = std::byteswap(v);
    store_native<T>(out, v);
}

template <class T, ByteLike B>
    requires(std::integral<T> && !std::same_as<T, bool> && !std::is_const_v<B>)
constexpr void store_be(std::span<B, sizeof(T)> out, T v) noexcept {
    if constexpr (std::endian::native != std::endian::big)
        v = std::byteswap(v);
    store_native<T>(out, v);
}

template <std::integral T, ByteLike B>
    requires(!std::same_as<T, bool>)
void load_le_run(std::span<B> bytes, std::span<T> out) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        const auto src = byte_cast<const std::byte>(bytes.first(out.size() * sizeof(T)));
        std::ranges::copy(src, std::as_writable_bytes(out).begin());
    } else {
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = load_le<T>(bytes.subspan(i * sizeof(T)).template first<sizeof(T)>());
        }
    }
}

template <std::integral T, ByteLike B>
    requires(!std::same_as<T, bool> && !std::is_const_v<B>)
void store_le_run(std::span<const T> words, std::span<B> bytes) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        const auto dst = byte_cast<std::byte>(bytes.first(words.size() * sizeof(T)));
        std::ranges::copy(std::as_bytes(words), dst.begin());
    } else {
        for (std::size_t i = 0; i < words.size(); ++i) {
            store_le<T>(bytes.subspan(i * sizeof(T)).template first<sizeof(T)>(), words[i]);
        }
    }
}

}
