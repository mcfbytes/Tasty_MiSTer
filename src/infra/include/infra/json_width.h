// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <limits>
#include <string_view>
#include <type_traits>

namespace mister::infra {

class JsonWidth {
public:
    constexpr JsonWidth() noexcept = default;

    constexpr explicit JsonWidth(std::size_t bytes) noexcept : bytes_(bytes) {}

    template <std::size_t N>
    [[nodiscard]] static consteval JsonWidth longest(
        const std::array<const char*, N>& names
    ) noexcept {
        std::size_t best = 0;
        for (const char* n : names) {
            const std::size_t len = std::string_view(n).size();
            if (len > best)
                best = len;
        }
        return JsonWidth{best};
    }

    template <std::size_t N, class Row>
        requires requires(const Row& r) { r.name; }
    [[nodiscard]] static consteval JsonWidth longest(const std::array<Row, N>& rows) noexcept {
        std::size_t best = 0;
        for (const Row& r : rows) {
            const std::size_t len = std::string_view(r.name).size();
            if (len > best)
                best = len;
        }
        return JsonWidth{best};
    }

    template <std::integral Rep>
    [[nodiscard]] static consteval JsonWidth digits() noexcept {
        constexpr std::size_t d = static_cast<std::size_t>(std::numeric_limits<Rep>::digits10) + 1;
        return JsonWidth{std::is_signed_v<Rep> ? d + 1 : d};
    }

    template <class S>
        requires requires { S::kCapacity; }
    [[nodiscard]] static consteval JsonWidth of() noexcept {
        return JsonWidth{S::kCapacity};
    }

    friend constexpr JsonWidth operator+(JsonWidth a, JsonWidth b) noexcept {
        return JsonWidth{a.bytes_ + b.bytes_};
    }

    friend constexpr bool operator==(JsonWidth a, JsonWidth b) noexcept {
        return a.bytes_ == b.bytes_;
    }

    friend constexpr bool operator<(JsonWidth a, JsonWidth b) noexcept {
        return a.bytes_ < b.bytes_;
    }

private:
    friend class JsonOut;
    std::size_t bytes_ = 0;
};

}
