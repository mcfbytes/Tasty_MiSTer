// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"
#include "proto/status_bit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "cores/cd_census_rows.h"
#include "cores/cd_core.h"
#include "cores/cd_io_indices.h"
#include "cores/cd_profile.h"
#include "cores/cd_tick_periods.h"
#include "cores/core_profile.h"
#include "svc/cue_policy.h"
#include "svc/search_policy.h"

namespace mister::cores::manifests {

extern const CoreProfile kPceCd;

inline constexpr auto kPceCdServices = std::to_array<LinkDecoderDecl>({
    {
        "pcecd.cmd_edge",
        reactor::Cause::Ring,
        &kCdCommandEdge<kPceCd>,

        reactor::kEveryRound,
        RoundClaim::Never,
    },
    {
        "pcecd.service_tick",
        reactor::Cause::Tick,
        &kCdServiceTick<kPceCd>,
        std::chrono::milliseconds{13},
        RoundClaim::Always,
    },
});

inline constexpr CdTickPeriods kPceCdTicks{
    .idle = std::chrono::microseconds{13'333},
    .stream = std::chrono::microseconds{16'000},
    .caller = std::chrono::microseconds{13'000},
    .speeds = 0,
    .speed = {},
};

inline constexpr CdIoIndices kPceCdIoIndices{
    .data = IoIndex{2},
    .cdda = IoIndex{3},
    .subcode = IoIndex{4},
    .info = std::nullopt,
};

inline constexpr svc::CuePolicy kPceCdCuePolicy{
    .type_detect = svc::CuePolicy::TypeDetect::PerTrack,
    .offset_op = svc::CuePolicy::OffsetOp::Assign,
    .sub_naming = svc::CuePolicy::SubNaming::ImageFilename,
    .wav_header_skip = true,
    .sniff_sega_disc_system = false,
    .subcode_shaping = svc::CuePolicy::SubcodeShaping::Interleave,
};

inline constexpr auto kPceCdSeekFastPaths = std::to_array<CdProfile::SeekFastPath>({
    {.count = SectorCount{120}, .latency_ticks = PollTicks{}},
    {.count = SectorCount{252}, .latency_ticks = PollTicks{5}},
});

inline constexpr CdProfile kPceCdProfile{
    .io = kPceCdIoIndices,
    .cue = kPceCdCuePolicy,
    .ticks = kPceCdTicks,

    .reset = proto::ResetTerms::framework().with_raised(proto::StatusBit{16}),
    .row = CdProfile::PceScsiFacts{.seek_fast_paths = kPceCdSeekFastPaths},
};

inline constexpr auto kPceCdSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "CUECHD",
     .label = "CD",
     .writable = false,
     .required = false,
     .role = SlotRole::Disc,
     .start_mount = StartMount::AsPick},
});

inline constexpr std::array<std::byte, 26> kPceUsBiosSig{
    std::byte{0x72}, std::byte{0xA2}, std::byte{0xC2}, std::byte{0x04}, std::byte{0x12},
    std::byte{0xF6}, std::byte{0xB6}, std::byte{0xA6}, std::byte{0x04}, std::byte{0xA2},
    std::byte{0x36}, std::byte{0xA6}, std::byte{0xC6}, std::byte{0x2E}, std::byte{0x4E},
    std::byte{0xF6}, std::byte{0x76}, std::byte{0x96}, std::byte{0xC6}, std::byte{0xCE},
    std::byte{0x34}, std::byte{0x32}, std::byte{0x2E}, std::byte{0x26}, std::byte{0x74},
    std::byte{0x00},
};

inline constexpr auto kPceCdSignatures = std::to_array<SignatureRow>({
    {.offset = 262144 - 26,
     .magic = kPceUsBiosSig,
     .region = proto::RegionCode{1},
     .why = "pcecd.cpp:128-151 us_sig sniff: present => bit-swapped US BIOS, "
            "region bit set (SetRegion, pcecd.cpp:188)"},
});

inline constexpr auto kPceCdBootAssets = std::to_array<BootAsset>({

    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageDir,
     .subdir = {},
     .dest = WideIoIndex{0},
     .group = BootGroup::First,
     .group_required = false},

    {.name = "cd_bios.rom",
     .where = kCoreHomeDir,
     .optional = true,
     .anchor = AssetAnchor::Search,
     .subdir = "TGFX16-CD",
     .dest = WideIoIndex{0},
     .group = BootGroup::First,
     .group_required = false},
});

inline constexpr std::array<std::byte, 8> kPceCdBramHeader{
    std::byte{'H'},
    std::byte{'U'},
    std::byte{'B'},
    std::byte{'M'},
    std::byte{0x00},
    std::byte{0x88},
    std::byte{0x10},
    std::byte{0x80},
};

inline constexpr svc::BlankSaveSpec kPceCdBlankSave{
    .fill = std::byte{0x00},
    .header = kPceCdBramHeader,
    .regions = {},
};

inline constexpr StagingPolicy kPceCdStaging{
    .order = StageOrder::PceCd,
    .reset_pulse_us = 100'000,
    .disc_slot = disc_slot_of(kPceCdSlots),
    .save = SaveMountPolicy{
        .slot = IoIndex{0},

    },
};

inline constexpr CoreProfile kPceCd{
    .kind = CoreKind::PceCd,
    .name = "TGFX16",
    .rbf = "TurboGrafx16",
    .services = kPceCdServices,
    .slots = kPceCdSlots,
    .boot_assets = kPceCdBootAssets,
    .signatures = kPceCdSignatures,
    .blank_save = kPceCdBlankSave,
    .staging = kPceCdStaging,
    .cue_browse_dir = "TGFX16-CD",
    .image_rows_no_zip = true,
    .cheats = {.cd_dir_suffix = "CD", .cd_asset_layout = true, .reset_on_remount = true},
    .reset = {.toggle_reacts = true, .button_reacts = true},
};

std::unique_ptr<Core> make_pcecd(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_pcecd_ladder(const CoreProfile& p, const LadderContext& ctx);

}
