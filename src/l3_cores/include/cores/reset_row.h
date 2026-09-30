// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "proto/reset_edge.h"
#include "proto/types.h"

namespace mister::cores {

struct ResetRow {
    bool toggle_reacts = false;
    bool button_reacts = false;
    bool toggle_ejects = false;

    std::optional<proto::StatusBit> remapped{};

    [[nodiscard]] constexpr bool reacts(const proto::ResetEdge& e) const noexcept {
        switch (e.source) {
            case proto::ResetSource::OsdToggle:
                return (toggle_reacts && e.toggles_reset_bit()) || remapped == e.bit;
            case proto::ResetSource::UserButton:
                return button_reacts;
            case proto::ResetSource::KbdCombo:
                return button_reacts && !e.cold;
        }
        return false;
    }

    [[nodiscard]] constexpr std::optional<proto::StatusBit> pulse(
        const proto::ResetEdge& e) const noexcept {
        if (e.source != proto::ResetSource::OsdToggle) return std::nullopt;
        return remapped == e.bit ? proto::StatusBit{0} : e.bit;
    }
    [[nodiscard]] constexpr bool ejects(const proto::ResetEdge& e) const noexcept {
        return toggle_ejects && e.toggles_reset_bit();
    }
};

}  // namespace mister::cores
