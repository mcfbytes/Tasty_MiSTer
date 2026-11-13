// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "proto/types.h"
#include "ui/shift_state.h"

namespace mister::ui {

class KeyboardText {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] static ShiftState shift_after(proto::KeyCode code, ShiftState now) noexcept;

    [[nodiscard]] static std::optional<char> of(proto::KeyCode code, ShiftState shift) noexcept;
};

}
