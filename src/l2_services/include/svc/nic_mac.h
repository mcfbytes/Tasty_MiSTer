// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace mister::svc {

struct MacAddress {
    static constexpr std::size_t kBytes = 6;
    static constexpr std::size_t kOuiBytes = 3;
    using Oui = std::array<std::byte, kOuiBytes>;

    using NicHalf = std::span<const std::byte, kBytes - kOuiBytes>;

    std::array<std::byte, kBytes> bytes{};

    [[nodiscard]] static MacAddress at(std::span<const std::byte> at) noexcept {
        MacAddress m;
        std::copy_n(at.begin(), kBytes, m.bytes.begin());
        return m;
    }

    [[nodiscard]] static constexpr MacAddress broadcast() noexcept {
        MacAddress m;
        m.bytes.fill(kAllOnes);
        return m;
    }

    [[nodiscard]] static constexpr MacAddress of_nic_half(NicHalf half) noexcept {
        MacAddress m;
        std::ranges::copy(half, m.bytes.begin() + kOuiBytes);
        return m;
    }

    [[nodiscard]] constexpr bool low_half_zero() const noexcept {
        return std::all_of(bytes.begin() + kOuiBytes, bytes.end(), [](std::byte b) {
            return b == kNoBits;
        });
    }

    [[nodiscard]] constexpr bool is_group() const noexcept {
        return (bytes.front() & kGroupBit) == kGroupBit;
    }

    [[nodiscard]] constexpr bool is_broadcast() const noexcept { return *this == broadcast(); }

    [[nodiscard]] constexpr MacAddress under(const Oui& oui) const noexcept {
        MacAddress m = *this;
        std::copy(oui.begin(), oui.end(), m.bytes.begin());
        return m;
    }

    [[nodiscard]] bool matches(std::span<const std::byte> at) const noexcept {
        return std::equal(bytes.begin(), bytes.end(), at.begin());
    }

    void write(std::span<std::byte> at) const noexcept {
        std::copy(bytes.begin(), bytes.end(), at.begin());
    }

    friend constexpr bool operator==(const MacAddress&, const MacAddress&) = default;

private:
    static constexpr std::byte kAllOnes{0xFF};
    static constexpr std::byte kNoBits{};
    static constexpr std::byte kGroupBit{0x01};
};

}
