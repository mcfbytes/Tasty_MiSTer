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
#include "svc/vfs.h"

namespace mister::cores::manifests {

extern const CoreProfile kN64;

inline constexpr svc::SearchDir kN64HomeDir[] = {svc::SearchDir::CoreDir};

inline constexpr std::uint32_t kRomLoadAddr = 0x3200'0000;
inline constexpr std::uint32_t kDdDiskLoadAddr = 0x3600'0000;
inline constexpr std::uint32_t kDdIplLoadAddr = 0x30C0'0000;

inline constexpr auto kN64StartAssets = std::to_array<BootAsset>({
    {.name = "boot0.rom", .where = kN64HomeDir, .subdir = "N64", .dest = WideIoIndex{0x00}},
    {.name = "boot1.rom", .where = kN64HomeDir, .subdir = "N64", .dest = WideIoIndex{0x40}},
    {.name = "boot2.rom", .where = kN64HomeDir, .subdir = "N64", .dest = WideIoIndex{0x80}},
    {.name = "boot.rom", .where = kN64HomeDir, .subdir = "N64", .dest = WideIoIndex{0x00}},
});

inline constexpr auto kN64Slots = std::to_array<FileSlot>({
    {N64Core::kRomIndex, "N64z64n64v64", "Load"},
    {N64Core::kDdDiskIndex, "NDDndd", "Load 64DD Disk"},
    {N64Core::kDdIplIndex, "N64z64n64v64binrom", "Load 64DD IPL"},
    {N64Core::kGbIndex, "GBCGB ", "Load GB-Transfer"},
});

inline constexpr auto kN64Windows = std::to_array<CoreWindowDecl>({
    {.region = {0x1000'0000u, 0x0080'0000u, "n64-rdram"}, .feeds_pcm = false},
});

inline constexpr auto kN64Services = std::to_array<LinkDecoderDecl>({
    {
        "n64.cheat_tick",
        reactor::Cause::Tick,
        &kCheatEngineTick<kN64>,
        16,
        DeadlineClass::A,
        OsdBudget::Pinned,
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

    .analog_reshape = infra::OptRef<const svc::IAnalogReshape>{n64::kJoyEmu},

    .windows = kN64Windows,

    .reset = {.toggle_reacts = true},

    .host = {.pause = PauseOnOsd::Always},
};

std::unique_ptr<Core> make_n64(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<ILoader> make_n64_loader(const LoaderContext& ctx);

}
