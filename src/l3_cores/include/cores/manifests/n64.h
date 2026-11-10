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
#include "cores/n64_analog.h"
#include "cores/n64_core.h"

namespace mister::cores::manifests {

extern const CoreProfile kN64;

inline constexpr std::uint32_t kRomLoadAddr = 0x3200'0000;
inline constexpr std::uint32_t kDdDiskLoadAddr = 0x3600'0000;
inline constexpr std::uint32_t kDdIplLoadAddr = 0x30C0'0000;

inline constexpr auto kN64StartAssets = std::to_array<BootAsset>({
    {.name = "boot0.rom", .where = kCoreHomeDir, .subdir = "N64", .dest = WideIoIndex{0x00}},
    {.name = "boot1.rom", .where = kCoreHomeDir, .subdir = "N64", .dest = WideIoIndex{0x40}},
    {.name = "boot2.rom", .where = kCoreHomeDir, .subdir = "N64", .dest = WideIoIndex{0x80}},
    {.name = "boot.rom", .where = kCoreHomeDir, .subdir = "N64", .dest = WideIoIndex{0x00}},
});

inline constexpr auto kN64Slots = std::to_array<FileSlot>({
    {.index = N64Core::kRomIndex, .extensions = "N64z64n64v64", .label = "Load"},
    {.index = N64Core::kDdDiskIndex, .extensions = "NDDndd", .label = "Load 64DD Disk"},
    {.index = N64Core::kDdIplIndex, .extensions = "N64z64n64v64binrom", .label = "Load 64DD IPL"},
    {.index = N64Core::kGbIndex, .extensions = "GBCGB ", .label = "Load GB-Transfer"},
});

inline constexpr auto kN64Windows = std::to_array<CoreWindowDecl>({
    {.region = {0x1000'0000u, 0x0080'0000u, "n64-rdram"}, .feeds_pcm = false},
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

    .analog_reshape = infra::OptRef<const svc::IAnalogReshape>{n64::kJoyEmu},

    .windows = kN64Windows,

    .reset = {.toggle_reacts = true},

    .host = {.pause = PauseOnOsd::Always},
};

std::unique_ptr<Core> make_n64(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<ILoader> make_n64_loader(const LoaderContext& ctx);

}
