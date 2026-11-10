// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"
#include "cores/cd_census_rows.h"
#include "cores/cd_profile.h"
#include "cores/core_profile.h"
#include "cores/loader_context.h"
#include "cores/manifests/megacd.h"
#include "cores/romset_alias_source.h"

namespace mister::cores::manifests {

extern const CoreProfile kNeoGeo;

[[nodiscard]] std::unique_ptr<IRomsetAliasSource> make_neogeo_alias_source();

std::unique_ptr<ILoader> make_neogeo_loader(const LoaderContext& ctx);

inline constexpr auto kNeoGeoServices = std::to_array<LinkDecoderDecl>({
    {
        "neogeo.cmd_edge",
        reactor::Cause::Ring,
        &kCdCommandEdge<kNeoGeo>,
        reactor::kEveryRound,
        RoundClaim::Never,
    },
    {
        "neogeo.service_tick",
        reactor::Cause::Tick,
        &kCdServiceTick<kNeoGeo>,
        std::chrono::milliseconds{1},
        RoundClaim::Always,
    },
});

inline constexpr std::uint8_t kNeoGeoCdCrcStart = 5;

inline constexpr bool kNeoGeoCdStatusCarriesIsData = false;

inline constexpr bool kNeoGeoCdReadyProbeSkipsMode1 = false;

inline constexpr bool kNeoGeoCdStreamOnDataTrack = true;

inline constexpr CdTickPeriods kNeoGeoCdTicks{
    .idle = std::chrono::microseconds{10'000},
    .stream = std::chrono::microseconds{10'000},
    .caller = std::chrono::microseconds{1'000},
    .speeds = 4,
    .speed = {
        std::chrono::microseconds{10'000},
        std::chrono::microseconds{5'000},
        std::chrono::microseconds{4'000},
        std::chrono::microseconds{2'000}
    },
};

inline constexpr CdProfile kNeoGeoCdProfile{
    .io = kMegaCdIoIndices,
    .cue = kMegaCdCuePolicy,
    .ticks = kNeoGeoCdTicks,
    .row = CdProfile::SegaCddFacts{
        .crc_start = kNeoGeoCdCrcStart,
        .status_carries_is_data = kNeoGeoCdStatusCarriesIsData,
        .ready_probe_skips_mode1 = kNeoGeoCdReadyProbeSkipsMode1,
        .stream_on_data_track = kNeoGeoCdStreamOnDataTrack,
    },
};

namespace detail {
inline constexpr CdProfile::SegaCddFacts kMegaCdRow = std::get<CdProfile::SegaCddFacts>(
    kMegaCdProfile.row
);
inline constexpr CdProfile::SegaCddFacts kNeoGeoRow = std::get<CdProfile::SegaCddFacts>(
    kNeoGeoCdProfile.row
);
}

static_assert(
    detail::kNeoGeoRow.crc_start != detail::kMegaCdRow.crc_start,
    "neogeocd.cpp:23,:39 seeds 5 where megacd.cpp:43,:77 seeds 0"
);
static_assert(
    detail::kNeoGeoRow.status_carries_is_data != detail::kMegaCdRow.status_carries_is_data,
    "megacd.cpp:46 ORs isData into word2 bit 8; neogeocd.cpp:42 does not"
);
static_assert(
    detail::kNeoGeoRow.ready_probe_skips_mode1 != detail::kMegaCdRow.ready_probe_skips_mode1,
    "megacd.cpp:288-290 skips the MODE1 probe; neogeocd.cpp:160-169 makes it"
);
static_assert(
    detail::kNeoGeoRow.stream_on_data_track != detail::kMegaCdRow.stream_on_data_track,
    "megacd.cpp:32 keys the fast cadence off CD-DA; neogeocd.cpp:92 off DATA"
);
static_assert(
    kNeoGeoCdTicks.speeds == 4 && kMegaCdTicks.speeds == 0,
    "neogeocd.cpp:97-108 is a four-entry speed table; MegaCD has none"
);
static_assert(
    kNeoGeoCdTicks.speed[0] == kNeoGeoCdTicks.idle
        && kNeoGeoCdTicks.speed[0] == kNeoGeoCdTicks.stream,
    "neogeocd.cpp:90: `interval = 10` is the default AND the speed-0 entry"
);
static_assert(
    kNeoGeoCdTicks.speed[0] > kNeoGeoCdTicks.speed[1]
        && kNeoGeoCdTicks.speed[1] > kNeoGeoCdTicks.speed[2]
        && kNeoGeoCdTicks.speed[2] > kNeoGeoCdTicks.speed[3],
    "neogeocd.cpp:97-108: a higher speed index is a shorter interval"
);
static_assert(
    kNeoGeoCdTicks.caller < kNeoGeoCdTicks.speed[3],
    "the row must run faster than the 2 ms pass, or the gate aliases it"
);

inline constexpr auto kNeoGeoSlots = std::to_array<FileSlot>({
    {.index = IoIndex{1},
     .extensions = "*  ",
     .label = "Load ROM set",
     .writable = false,
     .required = false,
     .romset_browse = std::string_view{"NEO"},
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
    .save = SaveMountPolicy{.slot = IoIndex{0}},
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
