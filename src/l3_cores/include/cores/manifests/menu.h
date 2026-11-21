// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"

namespace mister::cores::manifests {

inline constexpr CoreProfile kMenu{
    .kind = CoreKind::Menu,
    .name = "MENU",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .start = StartRule::None,
    .signatures = {},
    .tasty_playable = true,
    .undeclared_start_mount = StartMount::Unmodelled,
    .host = {.front_end = true},
};

static_assert(kMenu.kind == CoreKind::Menu, "menu.sv:61 CONF_STR field 0 is MENU");
static_assert(kMenu.name == "MENU", "the registry row and CONF_STR field 0 (menu.sv:61)");
static_assert(kMenu.rbf.empty(), "menu.rbf is SD-root-only (file_io.cpp:1183), never a stem");
static_assert(kMenu.host.front_end, "the front end is user_io.cpp:217-221 is_menu()");
static_assert(
    !kMenu.suppresses_mgl,
    "MGL gating is `host.front_end` (identity_record.cpp:41); `suppresses_mgl` has its "
    "first consumer in AtariST"
);
static_assert(
    kMenu.services.empty() && kMenu.slots.empty() && kMenu.boot_assets.empty(),
    "MENU declares no services, slots or boot assets (Menu.md)"
);
static_assert(
    kMenu.signatures.empty() && kMenu.start == StartRule::None,
    "MENU has no signature rows and no start rule"
);

}
