// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"

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
#include "svc/disc_engine.h"

namespace mister::cores::manifests {

extern const CoreProfile kMegaCd;

inline constexpr auto kMegaCdServices = std::to_array<LinkDecoderDecl>({
    {
        "megacd.cmd_edge",
        reactor::Cause::Ring,
        &kCdCommandEdge<kMegaCd>,
        0,

        RoundClaim::Never,
    },
    {
        "megacd.service_tick",
        reactor::Cause::Blk,
        &kCdServiceTick<kMegaCd>,
        10,
        RoundClaim::Always,
    },
});

inline constexpr CdTickPeriods kMegaCdTicks{
    .idle_us = 13'333,
    .stream_us = 10'000,
    .caller_us = 10'000,
    .speeds = 0,
    .speed_us = {0, 0, 0, 0},
};

inline constexpr CdIoIndices kMegaCdIoIndices{
    .data = IoIndex{2},
    .cdda = IoIndex{4},
    .subcode = IoIndex{3},
};

inline constexpr svc::CuePolicy kMegaCdCuePolicy{
    .type_detect = svc::CuePolicy::TypeDetect::Track0Only,
    .offset_op = svc::CuePolicy::OffsetOp::Accumulate,
    .sub_naming = svc::CuePolicy::SubNaming::LastTrackFilename,
    .wav_header_skip = false,
    .sniff_sega_disc_system = true,
};

inline constexpr CdProfile kMegaCdProfile{
    .dialect = CdDialect::SegaBcdNibble,
    .crc_start = 0,
    .io = kMegaCdIoIndices,
    .cue = kMegaCdCuePolicy,
    .ticks = kMegaCdTicks,
    .sniff_sega_disc_system = true,
    .seek_fast_paths = {},
};

inline constexpr auto kMegaCdSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "CUECHD",
     .label = "CD",
     .writable = false,
     .required = false,
     .role = SlotRole::Disc,
     .start_mount = StartMount::AsPick},
});

inline constexpr std::array<std::byte, 14> kSegaDiscSystem{
    std::byte{'S'},
    std::byte{'E'},
    std::byte{'G'},
    std::byte{'A'},
    std::byte{'D'},
    std::byte{'I'},
    std::byte{'S'},
    std::byte{'C'},
    std::byte{'S'},
    std::byte{'Y'},
    std::byte{'S'},
    std::byte{'T'},
    std::byte{'E'},
    std::byte{'M'},
};

inline constexpr auto kMegaCdSignatures = std::to_array<SignatureRow>({
    {.offset = 0,
     .magic = kSegaDiscSystem,
     .region = 0,
     .why = "req sector-size sniff (megacdd.cpp:285-296): "
            "present => 2048, absent => 2352"},
});

namespace megacd_blank {
using B = std::byte;

inline constexpr std::array<B, 64> kMegaCdIntFormat{{
    B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F},
    B{0x5F}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x40}, B{0x00}, B{0x7D}, B{0x00}, B{0x7D},
    B{0x00}, B{0x7D}, B{0x00}, B{0x7D}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x53}, B{0x45}, B{0x47}, B{0x41}, B{0x5F}, B{0x43}, B{0x44}, B{0x5F},
    B{0x52}, B{0x4F}, B{0x4D}, B{0x00}, B{0x01}, B{0x00}, B{0x00}, B{0x00}, B{0x52}, B{0x41},
    B{0x4D}, B{0x5F}, B{0x43}, B{0x41}, B{0x52}, B{0x54}, B{0x52}, B{0x49}, B{0x44}, B{0x47},
    B{0x45}, B{0x5F}, B{0x5F}, B{0x5F},
}};

inline constexpr std::array<B, 64> kMegaCdExtFormat{{
    B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F}, B{0x5F},
    B{0x5F}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x40}, B{0x1F}, B{0xFD}, B{0x1F}, B{0xFD},
    B{0x1F}, B{0xFD}, B{0x1F}, B{0xFD}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00}, B{0x00},
    B{0x00}, B{0x00}, B{0x53}, B{0x45}, B{0x47}, B{0x41}, B{0x5F}, B{0x43}, B{0x44}, B{0x5F},
    B{0x52}, B{0x4F}, B{0x4D}, B{0x00}, B{0x01}, B{0x00}, B{0x00}, B{0x00}, B{0x52}, B{0x41},
    B{0x4D}, B{0x5F}, B{0x43}, B{0x41}, B{0x52}, B{0x54}, B{0x52}, B{0x49}, B{0x44}, B{0x47},
    B{0x45}, B{0x5F}, B{0x5F}, B{0x5F},
}};
}

inline constexpr std::uint32_t kMegaCdIntLba = 0xF;
inline constexpr std::uint32_t kMegaCdExtLba = 0x40F;
inline constexpr std::uint32_t kMegaCdFormatOffset = 448;

inline constexpr auto kMegaCdBlankRegions = std::to_array<BlankRegion>({
    {.offset_bytes = kMegaCdIntLba * 512 + kMegaCdFormatOffset,
     .bytes = megacd_blank::kMegaCdIntFormat},
    {.offset_bytes = kMegaCdExtLba * 512 + kMegaCdFormatOffset,
     .bytes = megacd_blank::kMegaCdExtFormat},
});

inline constexpr BlankSaveSpec kMegaCdBlankSave{
    .fill = std::byte{0x00},
    .header = {},
    .regions = kMegaCdBlankRegions,
};

inline constexpr svc::SearchDir kMegaCdHomeDir[] = {svc::SearchDir::CoreDir};

inline constexpr auto kMegaCdBootAssets = std::to_array<BootAsset>({

    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageParentDir,
     .subdir = {},
     .dest = WideIoIndex{0},
     .group = 0,
     .group_required = true},

    {.name = "boot.rom",
     .where = kMegaCdHomeDir,
     .optional = true,
     .anchor = AssetAnchor::Search,
     .subdir = "MEGACD",
     .dest = WideIoIndex{0},
     .group = 0,
     .group_required = true},

    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageDir,
     .subdir = {},
     .dest = WideIoIndex{0},
     .group = 1},

    {.name = "cart.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageDir,
     .subdir = {},
     .dest = WideIoIndex{0x40},
     .group = 2},
});

inline constexpr StagingPolicy kMegaCdStaging{
    .order = StageOrder::MegaCd,
    .reset_pulse_us = 0,
    .disc_slot = disc_slot_of(kMegaCdSlots),
    .save_slot = IoIndex{0},
    .save_dest = WideIoIndex{5},
};

inline constexpr CoreProfile kMegaCd{
    .kind = CoreKind::MegaCd,
    .name = "MEGACD",
    .rbf = "MegaCD",
    .services = kMegaCdServices,
    .slots = kMegaCdSlots,
    .boot_assets = kMegaCdBootAssets,
    .signatures = kMegaCdSignatures,
    .blank_save = kMegaCdBlankSave,
    .staging = kMegaCdStaging,
    .image_rows_no_zip = true,

    .cheats = {.cd_asset_layout = true},

    .reset = {.button_reacts = true, .toggle_ejects = true, .remapped = proto::StatusBit{1}},
};

std::unique_ptr<Core> make_megacd(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_megacd_ladder(const CoreProfile& p, const LadderContext& ctx);

}
