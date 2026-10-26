// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

struct UioFrameLane {
    enum class Reg : std::uint32_t {
        Begin = 0x00,
        Word = 0x04,
        End = 0x08,
        Played = 0x10,
        Drops = 0x14,
        Id = 0x18,
    };
    enum class Sel : std::uint32_t { Uio = 0, Fpga = 1, OsdHdmi = 2, OsdVga = 3 };

    static constexpr std::size_t kSize = 0x20;
    static constexpr std::uint32_t kId = 0x314E414Cu;
    static constexpr std::uint32_t kSelShift = 16;

    static constexpr std::uint32_t begin(Sel sel, std::uint16_t op) noexcept {
        return (static_cast<std::uint32_t>(sel) << kSelShift) | op;
    }
};

static_assert(
    static_cast<std::size_t>(UioFrameLane::Reg::Id) + 4 <= UioFrameLane::kSize
        && static_cast<std::size_t>(UioFrameLane::Reg::End)
            < static_cast<std::size_t>(UioFrameLane::Reg::Played),
    "lw_lane.sv's register map"
);
static_assert(
    UioFrameLane::begin(UioFrameLane::Sel::OsdVga, 0xFFFF) == 0x3FFFFu
        && UioFrameLane::begin(UioFrameLane::Sel::Uio, 0x1E) == 0x1Eu,
    "BEGIN is {sel[1:0], op[15:0]}"
);

}
