// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/indexed_array.h"
#include "svc/analog_xy.h"
#include "svc/key_edge.h"
#include "svc/stick_index.h"
#include "svc/types.h"

namespace mister::svc {

struct DeviceReport {
    JoyMask buttons{};
    bool buttons_changed = false;
    JoyMask menu_buttons{};
    infra::IndexedArray<AnalogXy, kSticks.size(), StickIndex> stick{};
    infra::IndexedArray<bool, kSticks.size(), StickIndex> stick_changed{};
    MouseMotion motion{};
    proto::MouseButtons mouse_buttons{};
    bool mouse_changed = false;

    KeyEdgeBatch keys{};
    std::uint16_t axis_edges = 0;
    std::uint16_t mouse_remainder = 0;
    std::uint16_t quirk_drops = 0;

    std::uint8_t osd_makes = 0;
    std::uint8_t osd_breaks = 0;
};

}
