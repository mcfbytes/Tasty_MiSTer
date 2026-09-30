// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>
#include <string_view>

namespace mister::os {

struct PhysAddr {
    std::uint64_t v = 0;
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
};

struct KernelIrq {
    std::uint32_t v = 0;
    friend constexpr bool operator==(KernelIrq, KernelIrq) = default;
};

struct CpuMask {
    std::uint32_t v = 0;
    friend constexpr bool operator==(CpuMask, CpuMask) = default;
};

}  // namespace mister::os
