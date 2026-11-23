// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app::input {

enum class KeyFocus : std::uint8_t { Core = 0, Menu = 1, MenuIniSwitch = 2, Terminal = 3 };

[[nodiscard]] constexpr bool raises_osd_gate(KeyFocus focus) noexcept {
    return focus == KeyFocus::Menu || focus == KeyFocus::MenuIniSwitch;
}

[[nodiscard]] constexpr bool bridges_keys(KeyFocus focus) noexcept {
    return focus == KeyFocus::Terminal;
}

using KeyFocusCell = xthread::Telemetry<KeyFocus, SeatTag::Ui>;

}
