// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pcecd_ladder.h"
#include "cores/manifests/pce.h"

#include "cores/manifests/megacd.h"

namespace mister::cores::manifests {

static_assert(kPceCdIoIndices.data == IoIndex{2}, "pcecdd.cpp:13: PCECD_DATA_IO_INDEX == 2");
static_assert(
    kPceCdIoIndices.cdda == IoIndex{3},
    "pcecdd.cpp:14: PCECD_CDDA_IO_INDEX == 3 (MegaCD uses 4)"
);
static_assert(
    kPceCdIoIndices.subcode == IoIndex{4},
    "pcecdd.cpp:15: PCECD_SUBCODE_IO_INDEX == 4 (MegaCD uses 3)"
);
static_assert(
    kPceCdIoIndices.info == IoIndex{},
    "pcecd publishes no disc descriptor: the info slot stays default"
);
static_assert(
    kPceCdIoIndices.cdda == kMegaCdIoIndices.subcode
        && kPceCdIoIndices.subcode == kMegaCdIoIndices.cdda,
    "req: the cdda/subcode indices are the SWAP of MegaCD's"
);
static_assert(kPceCdIoIndices.data == kMegaCdIoIndices.data, "req: the data index is the shared 2");

static_assert(
    kPceCdProfile.dialect == CdDialect::PceScsi,
    "req: PCE-CD speaks SCSI sub-opcodes over UIO_CD_GET"
);

static_assert(
    kPceCdTicks.stream_us == 16'000,
    "pcecd.cpp:28-29: 16.0 ms between frames in READ state"
);
static_assert(
    kPceCdTicks.idle_us == 13'333,
    "pcecd.cpp:31-33: 13 + ((adj==3)?1:0) walks 14/13/13 — the "
    "13.333 ms Red Book average, the same dither as req"
);
static_assert(
    kPceCdTicks.stream_us > kPceCdTicks.idle_us,
    "pcecd.cpp:28-31: the PCE READ cadence is SLOWER than idle — "
    "inverted relative to MegaCD, and contract"
);

static_assert(
    kPceCdTicks.caller_us == kPceCdServices[1].period_ms * 1000u,
    "caller_us must BE the service row's own period_ms"
);

static_assert(kPceCdTicks.speeds == 0, "PCE-CD engages no speed-indexed override");

static_assert(
    kPceCdCuePolicy.type_detect == svc::CuePolicy::TypeDetect::PerTrack,
    "pcecdd.cpp:154-183: every track's type line is read"
);
static_assert(
    kPceCdCuePolicy.offset_op == svc::CuePolicy::OffsetOp::Assign,
    "pcecdd.cpp:216,227: offset ASSIGNED against the track's real "
    "sector size (MegaCD accumulates a hardcoded 2352)"
);
static_assert(
    kPceCdCuePolicy.sub_naming == svc::CuePolicy::SubNaming::ImageFilename,
    "pcecdd.cpp:283-295: <image>.sub, outside the Toc — a "
    "user-visible on-disk contract MegaCD disagrees with"
);
static_assert(kPceCdCuePolicy.wav_header_skip, "pcecdd.cpp:129: the .wav sniff sets hdr = 44");
static_assert(
    !kPceCdProfile.sniff_sega_disc_system && !kPceCdCuePolicy.sniff_sega_disc_system,
    "req is MegaCD-only; PCE-CD never sniffs"
);

static_assert(
    kPceCdProfile.seek_fast_paths.size() == 2,
    "req: exactly the two per-title fast paths"
);
static_assert(
    kPceCdSeekFastPaths[0].count == 120 && kPceCdSeekFastPaths[0].latency_ticks == 0,
    "pcecdd.cpp:657-661: HuVideo cnt==120 -> latency 0"
);
static_assert(
    kPceCdSeekFastPaths[1].count == 252 && kPceCdSeekFastPaths[1].latency_ticks == 5,
    "pcecdd.cpp:663-667: Sherlock Holmes cnt==252 -> latency 5"
);

static_assert(kPceCdServices.size() == 2, "ARCHITECTURE.md 3.4: pcecd has exactly two census rows");
static_assert(
    kPceCdServices[0].cause == reactor::Cause::Ring,
    "3.4: the ungated CD_GET command edge is a RING doorbell"
);
static_assert(
    kPceCdServices[1].cause == reactor::Cause::Tick,
    "3.4: the CD service tick stays a ticker"
);
static_assert(
    kPceCdServices[0].osd_budget == OsdBudget::Shared
        && kPceCdServices[1].osd_budget == OsdBudget::Pinned,
    "the every-round edge shares the OSD budget; the service tick pins it"
);
static_assert(
    kPceCdServices[0].klass == DeadlineClass::A && kPceCdServices[1].klass == DeadlineClass::A,
    "3.4: both pcecd services are Class A (T-RT)"
);

static_assert(
    kPceCdServices[0].period_ms == 0,
    "the command edge runs EVERY ROUND: Cause::Ring never "
    "dispatches, and stock reads UIO_CD_GET ungated"
);
static_assert(
    kPceCdServices[1].period_ms != 0,
    "3.3/3.4: period_ms == 0 is an ARGUED escape hatch, not a "
    "default -- it costs an SPI transaction every T-RT round"
);

static_assert(kPceCd.kind == CoreKind::PceCd);
static_assert(
    kPceCd.name == std::string_view{"TGFX16"},
    "CONF_STR field 0 / cdring.h kCdRingCoreName: one spelling"
);
static_assert(kPceCd.services.size() == kPceCdServices.size());

static_assert(
    kPceCdServices[0].impl->active() && kPceCdServices[1].impl->active(),
    "3.4: both rows are unconditionally active"
);

static_assert(kPceUsBiosSig.size() == 26, "pcecd.cpp:128-132: the us_sig literal is 26 bytes");
static_assert(
    kPceCdSignatures[0].offset == 262144 - 26,
    "pcecd.cpp:148: compared at start + 262144 - 26"
);

static_assert(
    kPceCdServices[0].impl == &kCdCommandEdge<kPceCd>
        && kPceCdServices[1].impl == &kCdServiceTick<kPceCd>,
    "the two rows are the SHARED CD obligations keyed to this "
    "profile, never a per-manifest ILinkDecoder copy"
);
static_assert(
    kPceCdServices[0].impl != kMegaCdServices[0].impl
        && kPceCdServices[1].impl != kMegaCdServices[1].impl,
    "sharing the SHELL must not share the IDENTITY: the two "
    "families' rows stay distinct objects (item / item gate 3)"
);

std::unique_ptr<Core> make_pcecd(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<CdCore>(p, kPceCdProfile, g.services);
}

std::unique_ptr<BootLadder> make_pcecd_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<PceCdLadder>(p, ctx);
}

static_assert(start_mount_of(kPceCdSlots, IoIndex{0}) == StartMount::AsPick);

}
