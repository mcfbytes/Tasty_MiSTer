// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstdint>
#include <string_view>

namespace mister::os {

struct PhysAddr {
    std::uint64_t v = 0;

    [[nodiscard]] constexpr std::array<std::uint16_t, 2> low_halves() const noexcept {
        constexpr unsigned kHalfBits = 16;
        return {static_cast<std::uint16_t>(v), static_cast<std::uint16_t>(v >> kHalfBits)};
    }

    friend constexpr bool operator==(PhysAddr, PhysAddr) = default;
    friend constexpr auto operator<=>(PhysAddr, PhysAddr) = default;
};

[[nodiscard]] constexpr std::uint32_t low32(PhysAddr a) noexcept {
    return static_cast<std::uint32_t>(a.v);
}

enum class UioLine : std::uint8_t {};

struct UioLineSpace {
    std::string_view node_prefix;
    std::uint32_t lines = 0;
    std::string_view sysfs_root = "/sys/class/uio";
    std::string_view dev_dir = "/dev";
};

struct KernelIrq {
    std::uint32_t v = 0;
    friend constexpr bool operator==(KernelIrq, KernelIrq) = default;
};

struct CpuMask {
    std::uint32_t v = 0;
    friend constexpr bool operator==(CpuMask, CpuMask) = default;
};

struct I2cAddr {
    std::uint8_t v = 0;
    friend constexpr bool operator==(I2cAddr, I2cAddr) = default;
};

struct I2cReg {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] constexpr I2cReg plus(I n) const noexcept {
        return I2cReg{static_cast<std::uint8_t>(v + n)};
    }

    friend constexpr bool operator==(I2cReg, I2cReg) = default;
};

}
