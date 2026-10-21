// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

struct MouseDelta {
    std::int32_t dx = 0, dy = 0, dz = 0;
    std::uint8_t buttons = 0;
    bool button_edge = false;
    bool moved = false;
};

struct MouseAccumulator {
    TASTY_SEAT_EXEMPT(component);
    std::uint32_t x = 0, y = 0, wheel = 0;
    std::uint32_t edges = 0;
    std::uint8_t buttons = 0;
    std::uint32_t wm_x = 0, wm_y = 0, wm_w = 0, wm_edges = 0;

    void accumulate(std::int32_t rx, std::int32_t ry, std::int32_t rw) noexcept {
        x += static_cast<std::uint32_t>(rx);
        y += static_cast<std::uint32_t>(ry);
        wheel += static_cast<std::uint32_t>(rw);
    }

    void note_buttons(std::uint8_t level) noexcept {
        if (level != buttons) {
            buttons = level;
            ++edges;
        }
    }

    MouseDelta take() noexcept {
        MouseDelta d{};
        d.dx = static_cast<std::int32_t>(x - wm_x);
        d.dy = static_cast<std::int32_t>(y - wm_y);
        d.dz = static_cast<std::int32_t>(wheel - wm_w);
        d.buttons = buttons;
        d.button_edge = (edges != wm_edges);
        d.moved = (d.dx != 0) || (d.dy != 0) || (d.dz != 0);
        wm_x = x;
        wm_y = y;
        wm_w = wheel;
        wm_edges = edges;
        return d;
    }
};

}
