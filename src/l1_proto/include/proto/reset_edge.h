// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto {

enum class ResetSource : std::uint8_t { OsdToggle = 1, UserButton = 2, KbdCombo = 3 };

struct ResetEdge {
    TASTY_SEAT_EXEMPT(component);
    ResetSource source{};
    StatusBit bit{};
    bool ex = false;
    bool cold = false;

    [[nodiscard]] static constexpr ResetEdge osd_toggle(StatusBit b, bool ext) noexcept {
        return ResetEdge{.source = ResetSource::OsdToggle, .bit = b, .ex = ext};
    }
    [[nodiscard]] static constexpr ResetEdge user_button() noexcept {
        return ResetEdge{.source = ResetSource::UserButton};
    }
    [[nodiscard]] static constexpr ResetEdge kbd_combo(bool held_shift) noexcept {
        return ResetEdge{.source = ResetSource::KbdCombo, .cold = held_shift};
    }

    [[nodiscard]] constexpr bool toggles_reset_bit() const noexcept {
        return source == ResetSource::OsdToggle && bit == StatusBit{0};
    }
    friend constexpr bool operator==(ResetEdge, ResetEdge) = default;
};
static_assert(sizeof(ResetEdge) == 4);

}  // namespace mister::proto
