// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"
#include "ui/types.h"

namespace mister::ui {

struct UiEvent {
    proto::KeyCode raw{};

    enum Button : std::uint16_t {
        None = 0,
        Menu = 1u << 0,
        Select = 1u << 1,
        Back = 1u << 2,
        Up = 1u << 3,
        Down = 1u << 4,
        Left = 1u << 5,
        Right = 1u << 6,
        Plus = 1u << 7,
        Minus = 1u << 8,
        Recent = 1u << 9,

        PadBack = 1u << 10,
    };

    UiButtonMask buttons{};

    std::uint16_t repeat_depth = 0;
};

}
