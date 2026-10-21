// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/neogeo.h"

#include <string_view>

#include "cores/manifests/megacd.h"
#include "cores/neogeo_alias_source.h"
#include "cores/neogeo_core.h"
#include "cores/neogeo_loader.h"
#include "cores/neogeocd_ladder.h"

namespace mister::cores::manifests {

std::unique_ptr<IRomsetAliasSource> make_neogeo_alias_source() {
    return std::make_unique<NeoGeoAliasSource>();
}

std::unique_ptr<ILoader> make_neogeo_loader(const LoaderContext& ctx) {
    return std::make_unique<NeoGeoLoader>(ctx.vfs, &ctx.profile);
}

std::unique_ptr<Core> make_neogeo(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<NeoGeoCore>(p, kNeoGeoCdProfile, g.services);
}

std::unique_ptr<BootLadder> make_neogeo_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<NeoGeoCdLadder>(p, ctx);
}

static_assert(
    kNeoGeo.windows.size() > NeoGeoLoader::kMemWindow
        && std::string_view{kNeoGeo.windows[NeoGeoLoader::kMemWindow].region.name} == "neogeo-mem",
    "neogeo_loader.cpp:240 and :300: the C-ROM pairs and the P-ROM land at 0x38000000"
);

static_assert(kNeoGeo.kind == CoreKind::NeoGeo);
static_assert(
    kNeoGeo.name == std::string_view{"NEOGEO"},
    "user_io.cpp:272: is_neogeo() is strcasecmp(orig_name, \"neogeo\")"
);
static_assert(kNeoGeo.rbf.empty(), "no rbf stem: the core is found by CONF_STR name");

static_assert(kNeoGeo.slots.size() == 2, "neogeo.sv CONF_STR: FS1 and S1, nothing else");
static_assert(
    kNeoGeo.slots[0].index == IoIndex{1} && kNeoGeo.slots[1].index == IoIndex{1},
    "hwpass spi-coretype 2026-09-05: `FS1,*,Load ROM set;S1,CUECHD,Load CD Image`"
);
static_assert(
    kNeoGeo.slots[0].extensions.size() % 3 == 0 && kNeoGeo.slots[1].extensions.size() % 3 == 0,
    "the packed 3-char grammar"
);
static_assert(
    kNeoGeo.slots[0].romset_browse && kNeoGeo.slots[0].no_enter,
    "menu.cpp:2429: is_neogeo() sets SCANO_NEOGEO | SCANO_NOENTER on the F arm"
);
static_assert(
    kNeoGeo.slots[0].romset_aliases == &make_neogeo_alias_source,
    "file_io.cpp:1543 + :1682: SCANO_NEOGEO scans romsets.xml and asks "
    "neogeo_get_altname per entry"
);
static_assert(
    !kNeoGeo.slots[1].romset_browse && kNeoGeo.slots[1].role == SlotRole::Disc,
    "menu.cpp:2833-2836: every Neo Geo S pick is neocd_set_image"
);
static_assert(
    walks_romset(kNeoGeo.slots, IoIndex{1}) && role_of(kNeoGeo.slots, IoIndex{1}) == SlotRole::Disc,
    "the shared index: an F pick walks the romset, an S mount is the disc"
);
static_assert(
    kNeoGeo.cue_browse_dir == std::string_view{"NeoGeo-CD"},
    "menu.cpp:463 + neogeocd.h:14: a CUE row browses NEOCD_DIR"
);

static_assert(
    kNeoGeoCdProfile.dialect == kMegaCdProfile.dialect,
    "neogeocd.cpp:13 drives megacd.h's cdd, the SAME machine"
);
static_assert(
    kNeoGeoCdProfile.io.data == kMegaCdIoIndices.data
        && kNeoGeoCdProfile.io.cdda == kMegaCdIoIndices.cdda
        && kNeoGeoCdProfile.io.subcode == kMegaCdIoIndices.subcode,
    "megacdd.cpp:1034,1041,1060: SectorSend's indices are compiled once; neogeo.sv "
    "downloads CD data on index 2 and CD-DA on 4"
);
static_assert(kNeoGeoCdProfile.crc_start == 5, "neogeocd.cpp:23 CRC_START 5");
static_assert(!kNeoGeoCdProfile.status_carries_is_data, "neogeocd.cpp:42: word 2 bit 8 stays 0");
static_assert(
    !kNeoGeoCdProfile.ready_probe_skips_mode1,
    "neogeocd.cpp:160-169: every sector type asks the core first"
);
static_assert(kNeoGeoCdProfile.stream_on_data_track, "neogeocd.cpp:92: the data track streams");
static_assert(
    kNeoGeoCdProfile.sniff_sega_disc_system == kMegaCdProfile.sniff_sega_disc_system,
    "the same LoadCUE (megacdd.cpp:285-296) sniffs for either core"
);

static_assert(kNeoGeoServices.size() == 2, "ARCHITECTURE.md 3.4.1: Neo Geo has two census rows");
static_assert(
    kNeoGeoServices[0].impl == &kCdCommandEdge<kNeoGeo>
        && kNeoGeoServices[1].impl == &kCdServiceTick<kNeoGeo>,
    "the SHARED CD obligations keyed to this profile"
);
static_assert(
    kNeoGeoServices[0].impl != kMegaCdServices[0].impl
        && kNeoGeoServices[1].impl != kMegaCdServices[1].impl,
    "sharing MegaCD's machine must not share its identity"
);
static_assert(
    kNeoGeoServices[0].cause == reactor::Cause::Ring && kNeoGeoServices[0].period_ms == 0,
    "neogeocd.cpp:54: the UIO_CD_GET edge is read on every poll, ungated"
);
static_assert(
    kNeoGeoServices[1].cause == reactor::Cause::Tick && kNeoGeoServices[1].period_ms == 1,
    "the gated pass's caller, below the 2 ms fastest arm (neogeocd.cpp:105-108)"
);
static_assert(
    kNeoGeoCdTicks.caller_us == kNeoGeoServices[1].period_ms * 1000u,
    "caller_us must BE the service row's own period_ms"
);
static_assert(
    kNeoGeoServices[0].klass == DeadlineClass::A && kNeoGeoServices[1].klass == DeadlineClass::A,
    "3.4: both CD services are Class A (T-RT)"
);

static_assert(
    kNeoGeo.staging.order == StageOrder::NeoGeoCd && kNeoGeo.staging.disc_slot == IoIndex{1},
    "menu.cpp:2833-2836: the S pick runs neocd_set_image"
);
static_assert(
    kNeoGeo.staging.save_slot == IoIndex{0} && kNeoGeo.staging.save_dest == WideIoIndex{},
    "neogeo_loader.cpp:1273 user_io_file_mount(path, 0, 1): SD 0, no bracket"
);
static_assert(
    !kNeoGeo.blank_save.declared() && kNeoGeo.blank_save.generic_ff,
    "support/neogeo synthesises no blank save: the generic 0xFF answer"
);
static_assert(
    !kNeoGeo.cheats.reset_on_mount,
    "neogeocd.cpp:12 includes cheats.h and calls nothing from it"
);
static_assert(
    kNeoGeo.reset.button_reacts && !kNeoGeo.reset.toggle_reacts,
    "user_io.cpp:3072 neocd_reset on the USER release; menu.cpp:2587-2600 has no arm"
);

}
