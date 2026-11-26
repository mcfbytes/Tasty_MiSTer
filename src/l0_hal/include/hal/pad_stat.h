// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/pad_block.h"
#include "hal/pad_entries.h"
#include "hal/reg_field.h"
#include "hal/reg_word.h"

namespace mister::hal {

class PadStat {
public:
    static constexpr RegField<std::uint32_t> kLevelField{16, 0x1Fu};
    static constexpr PadEntries kDepth{PadBlock::kSpinnerFifoDepth};

    constexpr PadStat() noexcept = default;

    [[nodiscard]] static constexpr PadStat of(RegWord w) noexcept { return PadStat{w.bits()}; }

    [[nodiscard]] constexpr PadEntries room() const noexcept {
        const PadEntries level{static_cast<std::uint8_t>(kLevelField.get(v_))};
        return level < kDepth ? kDepth.minus(level) : PadEntries{};
    }

    friend constexpr bool operator==(PadStat, PadStat) = default;

private:
    constexpr explicit PadStat(std::uint32_t word) noexcept : v_(word) {}

    std::uint32_t v_ = 0;
};

static_assert(
    PadStat::of(RegWord{0x00100000u}).room() == PadEntries{},
    "a level of 16 leaves no room"
);
static_assert(PadStat::of(RegWord{0x0000FFFFu}).room() == PadStat::kDepth, "drops are not level");

}
