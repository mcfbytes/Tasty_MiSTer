// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/uio_frame_lane.h"
#include "infra/error.h"

namespace mister::proto {

using FrameSel = hal::UioFrameLane::Sel;

class ILinkFrame {
public:
    virtual ~ILinkFrame() = default;

    [[nodiscard]] virtual Ex<void> begin(FrameSel sel, std::uint16_t op) = 0;

    [[nodiscard]] virtual Ex<void> word(std::uint16_t w) = 0;

    [[nodiscard]] virtual Ex<void> end() = 0;

protected:
    ILinkFrame() = default;
    ILinkFrame(const ILinkFrame&) = default;
    ILinkFrame& operator=(const ILinkFrame&) = default;
};

}
