// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"
#include "cores/cd_census_rows.h"
#include "cores/cd_profile.h"
#include "cores/core_profile.h"
#include "cores/loader_context.h"
#include "cores/manifests/neogeocd_fragment.h"

namespace mister::cores::manifests {

extern const CoreProfile kNeoGeo;

[[nodiscard]] std::unique_ptr<IRomsetAliasSource> make_neogeo_alias_source();

std::unique_ptr<ILoader> make_neogeo_loader(const LoaderContext& ctx);

inline constexpr auto kNeoGeoServices = std::to_array<LinkDecoderDecl>({
    {
        "neogeo.cmd_edge",
        reactor::Cause::Ring,
        &kCdCommandEdge<kNeoGeo>,
        0,
        RoundClaim::Never,
    },
    {
        "neogeo.service_tick",
        reactor::Cause::Tick,
        &kCdServiceTick<kNeoGeo>,
        1,
        RoundClaim::Always,
    },
});

inline constexpr CdProfile kNeoGeoCdProfile{
    .dialect = CdDialect::SegaBcdNibble,
    .crc_start = kNeoGeoCdCrcStart,
    .status_carries_is_data = kNeoGeoCdStatusCarriesIsData,
    .ready_probe_skips_mode1 = kNeoGeoCdReadyProbeSkipsMode1,
    .stream_on_data_track = kNeoGeoCdStreamOnDataTrack,
    .io = kMegaCdIoIndices,
    .cue = kMegaCdCuePolicy,
    .ticks = kNeoGeoCdTicks,
    .sniff_sega_disc_system = true,
    .seek_fast_paths = {},
};

inline constexpr auto kNeoGeoSlots = std::to_array<FileSlot>({
    {.index = IoIndex{1},
     .extensions = "*  ",
     .label = "Load ROM set",
     .writable = false,
     .required = false,
     .romset_browse = true,
     .no_enter = true,
     .romset_aliases = &make_neogeo_alias_source},
    {.index = IoIndex{1},
     .extensions = "CUECHD",
     .label = "Load CD Image",
     .writable = false,
     .required = false,
     .role = SlotRole::Disc},
});

inline constexpr auto kNeoGeoWindows = std::to_array<CoreWindowDecl>({
    {.region = {0x1800'0000u, 0u, "neogeo-mem"}, .feeds_pcm = false},
    {.region = {0x1000'0000u, 0u, "neogeo-rom"}, .feeds_pcm = false},
});

inline constexpr StagingPolicy kNeoGeoStaging{
    .order = StageOrder::NeoGeoCd,
    .reset_pulse_us = 0,
    .disc_slot = disc_slot_of(kNeoGeoSlots),
    .save_slot = IoIndex{0},
};

inline constexpr CoreProfile kNeoGeo{
    .kind = CoreKind::NeoGeo,
    .name = "NEOGEO",
    .rbf = "",
    .services = kNeoGeoServices,
    .slots = kNeoGeoSlots,
    .boot_assets = {},
    .signatures = {},

    .blank_save = {.fill = std::byte{0xFF}, .header = {}, .regions = {}, .generic_ff = true},
    .staging = kNeoGeoStaging,
    .cue_browse_dir = "NeoGeo-CD",
    .image_rows_no_zip = true,

    .cheats = {.reset_on_mount = false},
    .windows = kNeoGeoWindows,

    .reset = {.button_reacts = true},
};

std::unique_ptr<Core> make_neogeo(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_neogeo_ladder(const CoreProfile& p, const LadderContext& ctx);

}
