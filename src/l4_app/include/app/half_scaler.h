// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/frame_arena.h"
#include "app/raw_frame_slot.h"
#include "app/rgb_rows.h"
#include "infra/seat.h"

namespace mister::app {

class HalfScaler {
    TASTY_SEAT_RESIDENT(Encode);

public:
    HalfScaler() noexcept = default;
    HalfScaler(const HalfScaler&) = delete;
    HalfScaler& operator=(const HalfScaler&) = delete;

    [[nodiscard]] RgbRows picture(const RawFrameSlot::Frame& s) noexcept;

    void release() noexcept { arena_.release(); }

private:
    FrameArena arena_{};
};

}
