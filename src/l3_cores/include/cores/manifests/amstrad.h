// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/boot_asset.h"
#include "cores/core_profile.h"

namespace mister::cores::manifests {

inline constexpr std::string_view kAmstradRomCarriers[] = {"eZZ", "eZ0"};

inline constexpr BootAsset kAmstradStartRows[] = {
    {.name = "boot.rom",
     .where = kCoreHomeDir,
     .anchor = AssetAnchor::CoreHome,
     .dest = WideIoIndex{},
     .dropped_by_exts = kAmstradRomCarriers},
    {.name = "boot.e",
     .where = kCoreHomeDir,
     .anchor = AssetAnchor::CoreHome,
     .dest = WideIoIndex::of(IoIndex{}, 1),
     .sweep = AssetSweep::HexPage},
    {.name = "boot0.e",
     .where = kCoreHomeDir,
     .anchor = AssetAnchor::CoreHome,
     .dest = WideIoIndex::of(IoIndex{}, 2),
     .sweep = AssetSweep::HexPage},
    {.name = "boot1.e",
     .where = kCoreHomeDir,
     .anchor = AssetAnchor::CoreHome,
     .dest = WideIoIndex::of(IoIndex{}, 3),
     .sweep = AssetSweep::HexPage},
};

inline constexpr CoreProfile kAmstrad{
    .kind = CoreKind::Amstrad,
    .name = "Amstrad",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .start_assets = kAmstradStartRows,
    .signatures = {},
    .undeclared_role = SlotRole::Save,
};

}
