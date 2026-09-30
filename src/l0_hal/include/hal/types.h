// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

namespace mister::hal {

struct SpiWord {
    std::uint16_t v = 0;
    friend constexpr bool operator==(SpiWord, SpiWord) = default;
};

struct SpiOutput {
    std::uint32_t v = 0;
    friend constexpr bool operator==(SpiOutput, SpiOutput) = default;
};

struct ButtonMask {
    std::uint16_t v = 0;
    friend constexpr bool operator==(ButtonMask, ButtonMask) = default;
};

struct LwOffset {
    std::uint32_t v = 0;
    friend constexpr bool operator==(LwOffset, LwOffset) = default;
    friend constexpr auto operator<=>(LwOffset, LwOffset) = default;
};

struct FabricAddr {
    std::uint32_t v = 0;
    friend constexpr bool operator==(FabricAddr, FabricAddr) = default;
    friend constexpr auto operator<=>(FabricAddr, FabricAddr) = default;
};

enum class Width : std::uint8_t { Byte = 0, Word = 1 };

struct FrameSeq {
    std::uint32_t v = 0;
    friend constexpr bool operator==(FrameSeq, FrameSeq) = default;
    friend constexpr auto operator<=>(FrameSeq, FrameSeq) = default;
};

}  // namespace mister::hal
