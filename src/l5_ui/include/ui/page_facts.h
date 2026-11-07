// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "ui/types.h"

namespace mister::ui {

struct PageFacts {

    RowCount osd_rows{kFullOsdRows};

    std::chrono::milliseconds auto_pop{0};

    bool has_title_strip = true;

    bool takes_input_focus = true;

    bool allows_global_hotkeys = true;
    bool allows_popups = false;
    bool counts_as_menu_present = true;

    bool allows_cfg_switch = false;

    bool wake_every_round = false;

    bool repeat_all_keys = false;

    friend constexpr bool operator==(const PageFacts&, const PageFacts&) = default;
};

inline constexpr PageFacts kDefaultPageFacts{};

}
