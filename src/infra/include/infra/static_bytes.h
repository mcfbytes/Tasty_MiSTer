// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::infra {

class StaticBytes {
public:
    constexpr StaticBytes() noexcept = default;

    template <std::size_t N>
    consteval explicit StaticBytes(const std::array<std::uint8_t, N>& table) noexcept
        : bytes_(table) {}

    [[nodiscard]] constexpr std::span<const std::uint8_t> view() const noexcept { return bytes_; }

    [[nodiscard]] constexpr bool empty() const noexcept { return bytes_.empty(); }

private:
    std::span<const std::uint8_t> bytes_{};
};

}
