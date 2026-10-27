// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/hd_look.h"
#include "app/hd_themes.h"
#include "svc/config_snapshot.h"

namespace mister::app {

[[nodiscard]] inline HdLook hd_look_from_ini(const svc::ConfigSnapshot& c) noexcept {
    const HdThemeDecl& old = hd_theme(c.hd_theme);
    return HdLook{
        .layout = c.hd_layout != kHdLookUnset ? c.hd_layout : old.layout,
        .colors = c.hd_colors != kHdLookUnset ? c.hd_colors : old.colors,
        .display = c.hd_game_display,
        .opacity = c.hd_menu_opacity
    };
}

}
