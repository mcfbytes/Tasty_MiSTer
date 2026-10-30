// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/megacd_ladder.h"
#include "cores/manifests/megacd.h"

namespace mister::cores::manifests {

static_assert(kMegaCdIoIndices.data == IoIndex{2}, "megacd.h:38: MCD_DATA_IO_INDEX == 2");
static_assert(
    kMegaCdIoIndices.cdda == IoIndex{4},
    "megacd.h:40: MCD_CDDA_IO_INDEX == 4 (PCE-CD uses 3)"
);
static_assert(
    kMegaCdIoIndices.subcode == IoIndex{3},
    "megacd.h:39: MCD_SUB_IO_INDEX == 3 (PCE-CD uses 4)"
);
static_assert(
    kMegaCdIoIndices.info == IoIndex{},
    "megacd publishes no disc descriptor: the info slot stays default"
);
static_assert(
    kMegaCdIoIndices.cdda != kMegaCdIoIndices.subcode,
    "req: the cdda/subcode pair must stay distinct"
);

static_assert(
    kMegaCdProfile.dialect == CdDialect::SegaBcdNibble,
    "req: MegaCD is the SegaBcdNibble dialect"
);
static_assert(
    kMegaCdProfile.crc_start == 0,
    "megacd.cpp:43,:77: MegaCD crc_start seed is 0 (NeoGeoCD is 5)"
);

static_assert(
    kMegaCdProfile.status_carries_is_data,
    "megacd.cpp:46: MegaCD ORs isData into word2 bit 8 (NeoGeoCD does not)"
);
static_assert(
    kMegaCdProfile.ready_probe_skips_mode1,
    "megacd.cpp:288-290: MegaCD's MODE1 ready probe has no round trip (NeoGeoCD's does)"
);
static_assert(
    !kMegaCdProfile.stream_on_data_track,
    "megacd.cpp:32: MegaCD streams the CD-DA track (NeoGeoCD streams the data track)"
);

static_assert(
    kMegaCdTicks.stream_us == 10'000,
    "megacd.cpp:32-35: the CD-DA audio-fill cadence is a fixed 10 ms"
);
static_assert(
    kMegaCdTicks.idle_us == 13'333,
    "megacd.cpp:37-38: 13 + (!adj ? 1 : 0) walks 14/13/13 — the "
    "13.333 ms Red Book average (req)"
);
static_assert(
    kMegaCdTicks.stream_us < kMegaCdTicks.idle_us,
    "req: streaming must be the tighter of the two budgets"
);

static_assert(
    kMegaCdTicks.caller_us == kMegaCdServices[1].period_ms * 1000u,
    "caller_us must BE the service row's own period_ms"
);

static_assert(kMegaCdTicks.speeds == 0, "MegaCD engages no speed-indexed override");

static_assert(
    kMegaCdCuePolicy.offset_op == svc::CuePolicy::OffsetOp::Accumulate,
    "megacdd.cpp:195,211: MegaCD accumulates (`+=`) a hardcoded "
    "2352; PCE-CD assigns (`=`) the track's real size"
);
static_assert(
    kMegaCdCuePolicy.sub_naming == svc::CuePolicy::SubNaming::LastTrackFilename,
    "megacdd.cpp:234-235: track05.bin -> track05.sub — a "
    "user-visible on-disk contract PCE-CD disagrees with"
);
static_assert(
    kMegaCdCuePolicy.type_detect == svc::CuePolicy::TypeDetect::Track0Only,
    "megacdd.cpp:147-167: MegaCD detects type/size on track 0 only"
);
static_assert(
    !kMegaCdCuePolicy.wav_header_skip,
    "req: the .wav hdr=44 sniff is PCE-CD's (pcecdd.cpp:129)"
);

static_assert(
    kMegaCdProfile.sniff_sega_disc_system && kMegaCdCuePolicy.sniff_sega_disc_system,
    "megacdd.cpp:289: MegaCD sniffs \"SEGADISCSYSTEM\" for sector size"
);
static_assert(kSegaDiscSystem.size() == 14, "megacdd.cpp:289: memcmp length is 14 bytes");
static_assert(
    kMegaCdSignatures[0].offset == 0 && kMegaCdSignatures[0].magic.size() <= 0x10,
    "megacdd.cpp:277-283: the sniff window is the first 0x10 bytes"
);

static_assert(
    kMegaCdProfile.seek_fast_paths.empty(),
    "megacdd.cpp:878-887: MegaCD has no per-title seek fast paths"
);

static_assert(
    kMegaCdSlots.size() == 1
        && kMegaCdSlots[0].index == IoIndex{0}
        && kMegaCdSlots[0].extensions == std::string_view{"CUECHD"}
        && kMegaCdSlots[0].role == SlotRole::Disc,
    "MegaCD.sv S0,CUECHD; megacdd.cpp:249-273 probes CUE then CHD"
);
static_assert(
    kMegaCd.staging.save_slot == IoIndex{0},
    "megacd.cpp:92-97 / req: the save virtual disk is SD slot 0"
);

static_assert(
    kMegaCdServices.size() == 2,
    "ARCHITECTURE.md 3.4: MegaCD has exactly two census rows"
);
static_assert(
    kMegaCdServices[0].cause == reactor::Cause::Ring,
    "3.4: the ungated CD_GET command edge is a RING doorbell"
);
static_assert(
    kMegaCdServices[1].cause == reactor::Cause::Blk,
    "the CD-DA fill path is the work a BLK raise announces; the 10 ms "
    "period is the fallback the executive retires under a live line"
);
static_assert(
    kMegaCdServices[0].round_claim == RoundClaim::Never
        && kMegaCdServices[1].round_claim == RoundClaim::Always,
    "the every-round edge never claims its round; the CD-DA tick claims each it runs (no OSD row)"
);

static_assert(
    kMegaCdServices[0].period_ms == 0,
    "the command edge runs EVERY ROUND: Cause::Ring never "
    "dispatches, and stock reads UIO_CD_GET ungated"
);
static_assert(
    kMegaCdServices[1].period_ms != 0,
    "3.3/3.4: period_ms == 0 is an ARGUED escape hatch, not a "
    "default -- it costs an SPI transaction every T-RT round"
);

static_assert(kMegaCd.kind == CoreKind::MegaCd);
static_assert(
    kMegaCd.name == std::string_view{"MEGACD"},
    "CONF_STR field 0 / registry row: one spelling"
);
static_assert(kMegaCd.services.size() == kMegaCdServices.size());

static_assert(
    kMegaCdServices[0].impl->active() && kMegaCdServices[1].impl->active(),
    "3.4: both rows are unconditionally active"
);

static_assert(
    kMegaCdServices[0].impl == &kCdCommandEdge<kMegaCd>
        && kMegaCdServices[1].impl == &kCdServiceTick<kMegaCd>,
    "the two rows are the SHARED CD obligations keyed to this "
    "profile, never a per-manifest ILinkDecoder copy"
);

std::unique_ptr<Core> make_megacd(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<CdCore>(p, kMegaCdProfile, g.services);
}

std::unique_ptr<BootLadder> make_megacd_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<MegaCdLadder>(p, ctx);
}

static_assert(start_mount_of(kMegaCdSlots, IoIndex{0}) == StartMount::AsPick);

}
