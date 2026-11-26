// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/ocram_types.h"
#include "hal/reg_field.h"
#include "hal/uio_frame_lane.h"

namespace mister::hal::ocram {

class LaneShape {
public:
    [[nodiscard]] static constexpr std::optional<LaneShape> of(std::uint32_t word) noexcept {
        if (!kFpgaSel.test(word))
            return std::nullopt;
        return LaneShape{word};
    }

    [[nodiscard]] constexpr bool serves(UioFrameLane::Sel sel) const noexcept {
        return mask_bit_(sel).test(v);
    }

    friend constexpr bool operator==(LaneShape, LaneShape) = default;

private:
    static constexpr RegField<std::uint32_t> kUioSel{16, 1};
    static constexpr RegField<std::uint32_t> kFpgaSel{17, 1};
    static constexpr RegField<std::uint32_t> kOsdHdmiSel{18, 1};
    static constexpr RegField<std::uint32_t> kOsdVgaSel{19, 1};

    [[nodiscard]] static constexpr RegField<std::uint32_t> mask_bit_(
        UioFrameLane::Sel sel
    ) noexcept {
        switch (sel) {
            case UioFrameLane::Sel::Uio:
                return kUioSel;
            case UioFrameLane::Sel::Fpga:
                return kFpgaSel;
            case UioFrameLane::Sel::OsdHdmi:
                return kOsdHdmiSel;
            case UioFrameLane::Sel::OsdVga:
                return kOsdVgaSel;
        }
        return kFpgaSel;
    }

    constexpr explicit LaneShape(std::uint32_t word) noexcept : v(word) {}

    std::uint32_t v = 0;
};

static_assert(
    LaneShape::of(0x0002'0004u).has_value(),
    "the built word {FPGA select, gap 4} decodes"
);
static_assert(
    !LaneShape::of(0x0001'0004u).has_value(),
    "a mask without the FPGA select is refused"
);
static_assert(
    LaneShape::of(0x000B'0004u)->serves(UioFrameLane::Sel::OsdVga)
        && !LaneShape::of(0x000B'0004u)->serves(UioFrameLane::Sel::OsdHdmi),
    "b without HDMI serves UIO, FPGA and VGA"
);

}
