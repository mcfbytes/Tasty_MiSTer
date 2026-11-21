// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "cores/core_grant.h"
#include "cores/cheat_engine_row.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/loader.h"
#include "cores/loader_context.h"
#include "cores/n64/n64_analog.h"
#include "cores/n64/n64_core.h"
#include "cores/n64/n64_load_map.h"
#include "infra/prelude.h"

namespace mister::cores::manifests {

extern const CoreProfile kN64;

using n64::kRomLoadAddr;
using n64::kDdDiskLoadAddr;
using n64::kDdIplLoadAddr;
using n64::kN64Windows;

inline constexpr auto kN64StartAssets = std::to_array<BootAsset>({
    {.name = "boot0.rom", .where = kCoreHomeDir, .subdir = "N64", .dest = WideIoIndex{0x00}},
    {.name = "boot1.rom", .where = kCoreHomeDir, .subdir = "N64", .dest = WideIoIndex{0x40}},
    {.name = "boot2.rom", .where = kCoreHomeDir, .subdir = "N64", .dest = WideIoIndex{0x80}},
    {.name = "boot.rom", .where = kCoreHomeDir, .subdir = "N64", .dest = WideIoIndex{0x00}},
});

inline constexpr auto kN64Slots = std::to_array<FileSlot>({
    {.index = n64::N64Core::kRomIndex, .extensions = "N64z64n64v64", .label = "Load"},
    {.index = n64::N64Core::kDdDiskIndex, .extensions = "NDDndd", .label = "Load 64DD Disk"},
    {.index = n64::N64Core::kDdIplIndex,
     .extensions = "N64z64n64v64binrom",
     .label = "Load 64DD IPL"},
    {.index = n64::N64Core::kGbIndex, .extensions = "GBCGB ", .label = "Load GB-Transfer"},
});

inline constexpr auto kN64Services = std::to_array<LinkDecoderDecl>({
    {
        "n64.cheat_tick",
        reactor::Cause::Tick,
        &kCheatEngineTick<kN64>,
        std::chrono::milliseconds{16},
        RoundClaim::Always,
    },
});

inline constexpr CoreProfile kN64{
    .kind = CoreKind::N64,
    .name = "N64",
    .rbf = "N64",
    .services = kN64Services,
    .slots = kN64Slots,
    .boot_assets = {},
    .start_assets = kN64StartAssets,
    .start = StartRule::Assets,
    .signatures = {},
    .saves_by_channel = true,

    .windows = kN64Windows,

    .reset = {.toggle_reacts = true},

    .host = {
        .pause = PauseOnOsd::Always,

        .analog_reshape = infra::OptRef<const svc::input::IAnalogReshape>{n64::kJoyEmu}
    },
};

std::unique_ptr<Core> make_n64(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<ILoader> make_n64_loader(const LoaderContext& ctx);

std::unique_ptr<svc::IStorageBackend> make_n64_save_store(
    const svc::Vfs& vfs,
    infra::OptRef<xthread::WakeFlag> activity
);

}
