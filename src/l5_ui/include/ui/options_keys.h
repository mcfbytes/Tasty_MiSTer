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
        k.menu = ev.buttons.intersects({UiEvent::Menu});
        k.select = ev.buttons.intersects({UiEvent::Select});
        k.plus = ev.buttons.intersects({UiEvent::Plus});
        k.minus = ev.buttons.intersects({UiEvent::Minus});
        k.back = ev.buttons.intersects({UiEvent::Back});
        k.left = ev.buttons.intersects({UiEvent::Left});
        k.right = ev.buttons.intersects({UiEvent::Right});
        k.recent = ev.buttons.intersects({UiEvent::Recent});
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
        return Transition::replace(PageId::System);
    if (keys.left)
        return Transition::replace(PageId::Misc);
    return std::nullopt;
}

}
