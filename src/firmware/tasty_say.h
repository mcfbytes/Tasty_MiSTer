// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>

namespace mister::fw {

using TastyLineBuf = std::array<char, 1600>;

namespace detail {

void emit(std::span<char> buf, std::size_t n) noexcept;

}

void tasty_say(std::string_view line) noexcept;

template <class... A>
[[nodiscard]] std::string_view tasty_render(
    std::span<char> buf,
    std::format_string<A...> f,
    A&&... a
) noexcept {
    if (buf.empty())
        return {};
    const std::ptrdiff_t cut = std::ssize(buf) - 1;
    const std::format_to_n_result<char*>
        r = std::format_to_n(buf.data(), cut, f, std::forward<A>(a)...);
    const std::ptrdiff_t n = r.size < cut ? r.size : cut;
    *(buf.data() + n) = '\0';
    return std::string_view{buf.data(), buf.data() + n};
}

template <class... A>
void tasty_sayf(std::format_string<A...> f, A&&... a) noexcept {
    TastyLineBuf buf;
    detail::emit(buf, tasty_render(buf, f, std::forward<A>(a)...).size());
}

}
