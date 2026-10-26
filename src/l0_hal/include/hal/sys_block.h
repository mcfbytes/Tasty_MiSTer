// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

struct SysBlock {
    enum class Reg : std::uint32_t {
        Cause = 0x00,
        Stflg = 0x04,
        Status0 = 0x08,
        Status1 = 0x0C,
        Status2 = 0x10,
        Status3 = 0x14,
        Id = 0x1C,
    };
    static constexpr std::size_t kSize = 0x20;
    static constexpr std::uint32_t kId = 0x31535453u;
    static constexpr std::uint32_t kSnapshotWords = 4;

    static constexpr std::uint32_t kCauseStatus = 1u << 0;

    static constexpr Reg status(std::uint32_t i) noexcept {
        return static_cast<Reg>(static_cast<std::uint32_t>(Reg::Status0) + 4u * i);
    }

    static constexpr std::uint8_t head(std::uint32_t stflg) noexcept {
        return static_cast<std::uint8_t>(stflg & 0xFFu);
    }

    static constexpr bool stflg_valid(std::uint32_t stflg) noexcept {
        return (stflg & 0x0000'FFF0u) == 0xA0u;
    }

    static constexpr std::uint8_t flag(std::uint32_t stflg) noexcept {
        return static_cast<std::uint8_t>(stflg & 0x0Fu);
    }
};

static_assert(
    static_cast<std::size_t>(SysBlock::status(SysBlock::kSnapshotWords - 1)) + 4 <= SysBlock::kSize
        && static_cast<std::size_t>(SysBlock::Reg::Id) + 4 == SysBlock::kSize,
    "lw_sys_status.sv's register map"
);
static_assert(
    SysBlock::stflg_valid(0x0001'00A1u)
        && SysBlock::flag(0x0001'00A1u) == 1
        && SysBlock::head(0x0001'00A1u) == 0xA1
        && !SysBlock::stflg_valid(0xDEADBEEFu),
    "STFLG {seq 1, A, flag 1}"
);

}
