// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/cd_census_rows.h"
#include "cores/cd_core.h"
#include "cores/cd_io_indices.h"
#include "cores/cd_profile.h"
#include "cores/cd_tick_periods.h"
#include "cores/core_profile.h"
#include "svc/disc_engine.h"

namespace mister::cores::manifests {

extern const CoreProfile kThreeDo;

inline constexpr auto kThreeDoServices = std::to_array<LinkDecoderDecl>({
    {
        "3do.cmd_edge",
        reactor::Cause::Ring,
        &kCdCommandEdge<kThreeDo>,
        3,
        RoundClaim::Always,
    },
    {
        "3do.service_tick",
        reactor::Cause::Tick,
        &kCdServiceTick<kThreeDo>,
        3,
        RoundClaim::Always,
    },
});

inline constexpr CdTickPeriods kThreeDoTicks{
    .idle_us = 3'000,
    .stream_us = 3'000,
    .caller_us = 3'000,
    .speeds = 0,
    .speed_us = {0, 0, 0, 0},
};

inline constexpr CdIoIndices kThreeDoIoIndices{
    .data = IoIndex{8},
    .cdda = IoIndex{},
    .subcode = IoIndex{},
    .info = IoIndex{0xC},
};

inline constexpr svc::CuePolicy kThreeDoCuePolicy{
    .type_detect = svc::CuePolicy::TypeDetect::PerTrackRunningSize,
    .offset_op = svc::CuePolicy::OffsetOp::Accumulate,
    .sub_naming = svc::CuePolicy::SubNaming::None,
    .wav_header_skip = false,
    .sniff_sega_disc_system = false,
    .layout = svc::CuePolicy::Layout::Console,
    .track_close = svc::CuePolicy::TrackClose::SentinelPregapRelative,
    .pregap_model = svc::CuePolicy::PregapModel::SingleAccumulator,
    .post_load_shift = svc::CuePolicy::PostLoadTocShift::None,
    .iso_sizing = svc::CuePolicy::IsoSizing::Declared2048,
};

inline constexpr CdProfile kThreeDoProfile{
    .dialect = CdDialect::ThreeDo,
    .crc_start = 0,
    .io = kThreeDoIoIndices,
    .cue = kThreeDoCuePolicy,
    .ticks = kThreeDoTicks,
    .sniff_sega_disc_system = false,
    .seek_fast_paths = {},
};

inline constexpr auto kThreeDoSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "CUEISOCHD",
     .label = "CD",
     .writable = false,
     .required = false,
     .role = SlotRole::Disc},
});

inline constexpr svc::SearchDir kThreeDoHomeDir[] = {svc::SearchDir::CoreDir};

inline constexpr std::string_view kThreeDoDiscWords[] = {"disc", "disk"};

inline constexpr auto kThreeDoBootAssets = std::to_array<BootAsset>({

    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageDir,
     .subdir = {},
     .dest = WideIoIndex{0},
     .group = 0},
    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageParentDir,
     .subdir = {},
     .dest = WideIoIndex{0},
     .group = 0},
    {.name = "boot.rom",
     .where = kThreeDoHomeDir,
     .optional = true,
     .anchor = AssetAnchor::Search,
     .subdir = "3DO",
     .dest = WideIoIndex{0},
     .group = 0},

    {.name = "kanji.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageDir,
     .subdir = {},
     .dest = WideIoIndex{3},
     .group = 1},
    {.name = "kanji.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageParentDir,
     .subdir = {},
     .dest = WideIoIndex{3},
     .group = 1},
});

namespace threedo_blank {
using B = std::byte;

inline constexpr std::array<B, 256> kSaveBlank{{
    B{0x01}, B{0x5A}, B{0x5A}, B{0x5A}, B{0x5A}, B{0x5A}, B{0x02}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x4E}, B{0x56}, B{0x52}, B{0x41}, B{0x4D}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0xFF}, B{0xFF}, B{0xFF}, B{0xFF}, B{0x00}, B{0x00}, B{0x00}, B{0x01},
    B{0x00}, B{0x00}, B{0x80}, B{0x00}, B{0xFF}, B{0xFF}, B{0xFF}, B{0xFE}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x01}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x84}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x85}, B{0x5A}, B{0x02}, B{0xB6}, B{0x00}, B{0x00}, B{0x00}, B{0x98},
    B{0x00}, B{0x00}, B{0x00}, B{0x98}, B{0x00}, B{0x00}, B{0x00}, B{0x14}, B{0x00}, B{0x00},
    B{0x00}, B{0x14}, B{0x7A}, B{0xA5}, B{0x65}, B{0xBD}, B{0x00}, B{0x00}, B{0x00}, B{0x84},
    B{0x00}, B{0x00}, B{0x00}, B{0x84}, B{0x00}, B{0x00}, B{0x7F}, B{0x68}, B{0x00}, B{0x00},
    B{0x00}, B{0x14}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
}};
}

inline constexpr BlankSaveSpec kThreeDoBlankSave{
    .fill = std::byte{0x00},
    .header = threedo_blank::kSaveBlank,
    .regions = {},
};

inline constexpr StagingPolicy kThreeDoStaging{
    .order = StageOrder::ThreeDo,
    .reset_pulse_us = 0,
    .disc_slot = disc_slot_of(kThreeDoSlots),
    .save_slot = IoIndex{0},
    .save_dest = WideIoIndex{4},
    .save_name_disc_words = kThreeDoDiscWords,
};

inline constexpr CoreProfile kThreeDo{
    .kind = CoreKind::ThreeDo,
    .name = "3DO",
    .rbf = "",
    .services = kThreeDoServices,
    .slots = kThreeDoSlots,
    .boot_assets = kThreeDoBootAssets,
    .signatures = {},
    .blank_save = kThreeDoBlankSave,
    .staging = kThreeDoStaging,
    .image_rows_no_zip = true,

    .cheats = {.reset_on_mount = false},

    .reset = {.toggle_reacts = true, .button_reacts = true},
};

std::unique_ptr<Core> make_threedo(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_threedo_ladder(const CoreProfile& p, const LadderContext& ctx);

}
