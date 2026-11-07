// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "ui/page_id.h"
#include "ui/transition.h"
#include "ui/ui_event.h"

namespace mister::ui {

struct OptionsKeys {
    bool menu = false;
    bool select = false;
    bool plus = false;
    bool minus = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool recent = false;

    [[nodiscard]] static OptionsKeys of(const UiEvent& ev) noexcept {
        OptionsKeys k;
        k.menu = ev.buttons.has_any({UiEvent::Menu});
        k.select = ev.buttons.has_any({UiEvent::Select});
        k.plus = ev.buttons.has_any({UiEvent::Plus});
        k.minus = ev.buttons.has_any({UiEvent::Minus});
        k.back = ev.buttons.has_any({UiEvent::Back});
        k.left = ev.buttons.has_any({UiEvent::Left});
        k.right = ev.buttons.has_any({UiEvent::Right});
        k.recent = ev.buttons.has_any({UiEvent::Recent});
        return k;
    }

    [[nodiscard]] bool no_row_key() const noexcept { return !select && !plus && !minus && !recent; }
};

[[nodiscard]] inline std::optional<Transition> ring_transition(
    bool at_root,
    OptionsKeys keys
) noexcept {
    if (!at_root)
        return std::nullopt;
    if (keys.right)
        return Transition{Transition::Kind::Replace, PageId::System};
    if (keys.left)
        return Transition{Transition::Kind::Replace, PageId::Misc, kMiscStorageRow};
    return std::nullopt;
}

}
