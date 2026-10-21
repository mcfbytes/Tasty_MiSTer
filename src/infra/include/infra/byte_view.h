// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace mister::infra {

static_assert(
    std::is_same_v<std::uint8_t, unsigned char>,
    "std::uint8_t must be unsigned char, or a uint8_t view would not alias"
);

template <class T>
concept ByteLike = std::is_same_v<std::remove_cv_t<T>, char>
    || std::is_same_v<std::remove_cv_t<T>, unsigned char>
    || std::is_same_v<std::remove_cv_t<T>, std::byte>;

template <ByteLike To, ByteLike From, std::size_t N>
    requires(std::is_const_v<To> || !std::is_const_v<From>)
[[nodiscard]] inline std::span<To, N> byte_cast(std::span<From, N> s) noexcept {
    return std::span<To, N>(reinterpret_cast<To*>(s.data()), s.size());
}

template <ByteLike B, std::size_t N>
[[nodiscard]] inline std::string_view as_text(std::span<B, N> s) noexcept {
    const auto c = byte_cast<const char>(s);
    return {c.data(), c.size()};
}

template <ByteLike B = const std::byte>
    requires std::is_const_v<B>
[[nodiscard]] inline std::span<B> text_bytes(std::string_view s) noexcept {
    return byte_cast<B>(std::span<const char>(s.data(), s.size()));
}

template <ByteLike B>
[[nodiscard]] inline std::string_view text_until_nul(const B* p, std::size_t cap) noexcept {
    const char* c = reinterpret_cast<const char*>(p);
    return {c, ::strnlen(c, cap)};
}

template <class T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] inline std::span<const std::byte, sizeof(T)> bytes_of(const T& v) noexcept {
    return std::as_bytes(std::span<const T, 1>(&v, 1));
}

[[nodiscard]] inline std::uintptr_t misalignment(const void* p, std::size_t a) noexcept {
    return reinterpret_cast<std::uintptr_t>(p) & (a - 1);
}

[[nodiscard]] inline bool is_aligned(const void* p, std::size_t a) noexcept {
    return misalignment(p, a) == 0;
}

}
