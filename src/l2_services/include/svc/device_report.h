// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "svc/analog_xy.h"
#include "svc/key_edge.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr unsigned kMaxKeyEdges = 32;

struct DeviceReport {
    JoyMask buttons{};
    bool buttons_changed = false;
    JoyMask menu_buttons{};
    AnalogXy stick[2]{};
    bool stick_changed[2]{false, false};
    int rel_x = 0, rel_y = 0, rel_wheel = 0;
    std::uint8_t mouse_buttons = 0;
    bool mouse_changed = false;
    std::uint8_t key_count = 0;
    std::uint16_t key_overflow = 0;
    std::uint16_t axis_edges = 0;
    std::uint16_t mouse_remainder = 0;
    std::uint16_t quirk_drops = 0;

    std::uint8_t osd_makes = 0;
    std::uint8_t osd_breaks = 0;
    std::array<KeyEdge, kMaxKeyEdges> keys{};
};

}  // namespace mister::svc
