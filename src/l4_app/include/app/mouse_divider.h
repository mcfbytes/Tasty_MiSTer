// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/mouse_accumulator.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

class MouseDivider {
    TASTY_SEAT_RESIDENT(Input);

public:
    struct Packet {
        std::int32_t dx = 0, dy = 0, dz = 0;
        std::uint8_t buttons = 0;
    };

    explicit MouseDivider(os::IClock& clock) noexcept : clock_(clock) {}

    MouseDivider(const MouseDivider&) = delete;
    MouseDivider& operator=(const MouseDivider&) = delete;

    void accumulate(std::int32_t rx, std::int32_t ry, std::int32_t rw) noexcept {
        acc_.accumulate(rx, ry, rw);
    }

    void note_buttons(std::uint8_t level) noexcept { acc_.note_buttons(level); }

    [[nodiscard]] std::optional<Packet> flush() noexcept;

private:
    os::IClock& clock_;
    MouseAccumulator acc_{};
    std::int64_t last_mouse_ns_ = 0;
    bool mouse_pending_ = false;
    std::int32_t carry_x_ = 0, carry_y_ = 0, carry_z_ = 0;
};

}
