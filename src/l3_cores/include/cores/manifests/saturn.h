// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "cores/cd_census_rows.h"
#include "cores/cd_core.h"
#include "cores/cd_io_indices.h"
#include "cores/cd_profile.h"
#include "cores/cd_quirk_row.h"
#include "cores/cd_tick_periods.h"
#include "cores/core_profile.h"
#include "cores/saturn_cue.h"
#include "svc/search_policy.h"

namespace mister::cores::manifests {

extern const CoreProfile kSaturn;

inline constexpr auto kSaturnServices = std::to_array<LinkDecoderDecl>({
    {
        "saturn.cmd_edge",
        reactor::Cause::Ring,
        &kCdCommandEdge<kSaturn>,
        std::chrono::milliseconds{1},
        RoundClaim::Always,
    },
    {
        "saturn.service_tick",
        reactor::Cause::Tick,
        &kCdServiceTick<kSaturn>,
        std::chrono::milliseconds{1},
        RoundClaim::Always,
    },
});

inline constexpr CdTickPeriods kSaturnTicks{
    .idle = std::chrono::microseconds{13'333},
    .stream = std::chrono::microseconds{13'333},
    .caller = std::chrono::microseconds{1'000},
    .speeds = 3,
    .speed = {
        std::chrono::microseconds{16'667},
        std::chrono::microseconds{13'333},
        std::chrono::microseconds{6'667},
        {}
    },
};

inline constexpr CdIoIndices kSaturnIoIndices{
    .data = IoIndex{8},
    .cdda = std::nullopt,
    .subcode = std::nullopt,
    .info = IoIndex{0xC},
};

inline constexpr auto kSaturnQuirks = std::to_array<CdQuirkRow>({
    {.offset = 0x20, .id = "T-8126H", .quirk = CdQuirk::TocStartsAtIndex1},
    {.offset = 0x20, .id = "T-8120G", .quirk = CdQuirk::TocStartsAtIndex1},
    {.offset = 0x20, .id = "T-8112H", .quirk = CdQuirk::TocStartsAtIndex1},
    {.offset = 0x20, .id = "T-99901G", .quirk = CdQuirk::TocStartsAtIndex1},
    {.offset = 0x20, .id = "T-8112G", .quirk = CdQuirk::TocStartsAtIndex1},
    {.offset = 0x20, .id = "T-5008H", .quirk = CdQuirk::ReadSkipsSeek},
    {.offset = 0x20, .id = "T-10609G", .quirk = CdQuirk::ReadSkipsSeek},
});

inline constexpr CdProfile kSaturnProfile{
    .io = kSaturnIoIndices,
    .cue = saturn::kSaturnCuePolicy,
    .ticks = kSaturnTicks,
    .row = CdProfile::SaturnFacts{.quirks = kSaturnQuirks},
};

inline constexpr auto kSaturnSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "CUECHD",
     .label = "Insert Disc",
     .writable = false,
     .required = false,
     .role = SlotRole::Disc},
    {.index = IoIndex{1},
     .extensions = "SAV",
     .label = "Mount Backup RAM",
     .writable = true,
     .required = false,
     .role = SlotRole::Save,
     .opens_zip = true},
});

inline constexpr auto kSaturnWindows = std::to_array<CoreWindowDecl>({
    {.region = {0x1100'0000u, 0x0000'4000u, "saturn-cd-buf"}, .feeds_pcm = false},
});

inline constexpr std::string_view kSaturnDiscWords[] = {"disc"};

inline constexpr auto kSaturnBootAssets = std::to_array<BootAsset>({
    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageDir,
     .subdir = {},
     .dest = WideIoIndex{0},
     .group = BootGroup::First},
    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageParentDir,
     .subdir = {},
     .dest = WideIoIndex{0},
     .group = BootGroup::First},
    {.name = "boot.rom",
     .where = kCoreHomeDir,
     .optional = true,
     .anchor = AssetAnchor::Search,
     .subdir = "Saturn",
     .dest = WideIoIndex{0},
     .group = BootGroup::First},
});

namespace saturn_blank {
consteval std::array<std::byte, 128> make_record() {
    constexpr std::string_view kText = "BackUpRam Format";
    std::array<std::byte, 128> out{};
    for (std::size_t rep = 0; rep < 4; ++rep) {
        for (std::size_t i = 0; i < kText.size(); ++i) {
            out[rep * 32 + 2 * i + 1] = static_cast<std::byte>(kText[i]);
        }
    }
    return out;
}

inline constexpr std::array<std::byte, 128> kRecord = make_record();

inline constexpr proto::Lba kSecondCopyLba{128};
inline constexpr std::array<svc::BlankRegion, 1> kSecondCopy{
    {{.block = kSecondCopyLba, .within = {}, .bytes = kRecord}}
};
}

inline constexpr svc::BlankSaveSpec kSaturnBlankSave{
    .fill = std::byte{0x00},
    .header = saturn_blank::kRecord,
    .regions = saturn_blank::kSecondCopy,
};

inline constexpr StagingPolicy kSaturnStaging{
    .order = StageOrder::Saturn,
    .reset_pulse_us = 0,
    .disc_slot = disc_slot_of(kSaturnSlots),
    .save = SaveMountPolicy{
        .slot = IoIndex{1},
        .bracket = WideIoIndex{4},
        .disc_words = kSaturnDiscWords,
    },
};

inline constexpr CoreProfile kSaturn{
    .kind = CoreKind::Saturn,
    .name = "Saturn",
    .rbf = "",
    .services = kSaturnServices,
    .slots = kSaturnSlots,
    .boot_assets = kSaturnBootAssets,
    .signatures = {},
    .blank_save = kSaturnBlankSave,
    .staging = kSaturnStaging,
    .image_rows_no_zip = true,

    .cheats = {.reset_on_mount = false},
    .windows = kSaturnWindows,

    .reset = {.toggle_reacts = true, .button_reacts = true},
};

std::unique_ptr<Core> make_saturn(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_saturn_ladder(const CoreProfile& p, const LadderContext& ctx);

}
