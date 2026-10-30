// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/threedo_ladder.h"
#include "cores/manifests/threedo.h"

#include <string_view>

#include "cores/manifests/megacd.h"
#include "cores/manifests/pce.h"

namespace mister::cores::manifests {

static_assert(kThreeDoIoIndices.data == IoIndex{8}, "3do.h:78: CD_DATA_IO_INDEX == 0x8");
static_assert(kThreeDoIoIndices.info == IoIndex{0xC}, "3do.h:79: CD_INFO_IO_INDEX == 0xC");
static_assert(
    kThreeDoIoIndices.cdda == IoIndex{} && kThreeDoIoIndices.subcode == IoIndex{},
    "3docdd.cpp:609-612: the CD-DA arm is commented out and no "
    "subcode is ever assembled, so neither slot exists"
);
static_assert(
    kThreeDoIoIndices.data != kMegaCdIoIndices.data,
    "the data slot is 8 here and 2 in both Sega/PCE families: the "
    "index map is per-family DATA, never a shared constant"
);

static_assert(
    kThreeDoProfile.dialect == CdDialect::ThreeDo,
    "spec 1.5.2.1: 3DO reads 4 command words and writes 4 status words"
);
static_assert(kThreeDoProfile.crc_start == 0, "3docdd.cpp: this dialect carries no BCD checksum");
static_assert(
    kThreeDoProfile.seek_fast_paths.empty(),
    "3docdd.cpp:486-534: no per-title seek fast paths in this family"
);

static_assert(
    kThreeDoCuePolicy.type_detect == svc::CuePolicy::TypeDetect::PerTrackRunningSize,
    "3docdd.cpp:148-166: LoadCUE types EVERY track and knows MODE2/2352, "
    "which the Track0Only program neither does nor can"
);

static_assert(
    kThreeDoTicks.idle_us == 3'000 && kThreeDoTicks.stream_us == 3'000,
    "3do.cpp:23,:43: `poll_timer += 3` gates the WHOLE pass, and "
    "3DO has no second state to speed up for"
);
static_assert(
    kThreeDoTicks.stream_us == kThreeDoTicks.idle_us,
    "the one family with no two-arm cadence: do not 'fix' this to "
    "match MegaCD's `stream < idle` or PCE's inversion"
);
static_assert(
    kThreeDoTicks.caller_us == kThreeDoServices[1].period_ms * 1000u,
    "caller_us must BE the service row's own period_ms"
);

static_assert(kThreeDoTicks.speeds == 0, "3DO engages no speed-indexed override");

static_assert(kThreeDoServices.size() == 2, "ARCHITECTURE.md 3.4: 3DO has exactly two census rows");
static_assert(
    kThreeDoServices[0].cause == reactor::Cause::Ring,
    "3.4: the CD_GET command edge is a RING doorbell"
);
static_assert(
    kThreeDoServices[1].cause == reactor::Cause::Tick,
    "3do.cpp:41-50 is a free-running pass no fabric request "
    "announces: a plain ticker, as PCE-CD's is"
);
static_assert(
    kThreeDoServices[0].round_claim == RoundClaim::Always
        && kThreeDoServices[1].round_claim == RoundClaim::Always,
    "both 3 ms rows claim each round they run (no OSD row)"
);
static_assert(
    kThreeDoServices[0].period_ms == 3 && kThreeDoServices[1].period_ms == 3,
    "3do.cpp:23: the edge read is INSIDE the 3 ms gate, so the edge "
    "row is NOT the period-0 every-round row MegaCD and PCE ship"
);
static_assert(
    kThreeDoServices[0].impl->active() && kThreeDoServices[1].impl->active(),
    "3.4: both rows are unconditionally active"
);
static_assert(
    kThreeDoServices[0].impl == &kCdCommandEdge<kThreeDo>
        && kThreeDoServices[1].impl == &kCdServiceTick<kThreeDo>,
    "the two rows are the SHARED CD obligations keyed to this "
    "profile, never a per-manifest ILinkDecoder copy"
);
static_assert(
    kThreeDoServices[0].impl != kMegaCdServices[0].impl
        && kThreeDoServices[0].impl != kPceCdServices[0].impl
        && kThreeDoServices[1].impl != kMegaCdServices[1].impl
        && kThreeDoServices[1].impl != kPceCdServices[1].impl,
    "sharing the SHELL must not share the IDENTITY: each family's "
    "rows stay distinct objects (item / item gate 3)"
);

static_assert(kThreeDo.kind == CoreKind::ThreeDo);
static_assert(
    kThreeDo.name == std::string_view{"3DO"},
    "user_io.cpp:427: is_3do() is strcasecmp(orig_name, \"3DO\")"
);
static_assert(kThreeDo.rbf.empty(), "support/3do carries no rbf-stem spelling to transcribe");
static_assert(kThreeDo.services.size() == kThreeDoServices.size());
static_assert(
    kThreeDo.signatures.empty(),
    "3do.cpp loads its BIOS unconditionally: there is no size gate "
    "and no magic sniff to transcribe"
);
static_assert(
    kThreeDo.cheats.cd_dir_suffix.empty()
        && !kThreeDo.cheats.cd_asset_layout
        && !kThreeDo.cheats.reset_on_remount
        && !kThreeDo.cheats.reset_on_mount,
    "3do.cpp:11 includes cheats.h and calls nothing; `is_3do()` "
    "appears in neither cheats.cpp nor file_io.cpp, and no arm of "
    "menu.cpp/user_io.cpp calls cheats_init for it"
);
static_assert(
    !kThreeDo.cheats.reset_on_mount
        && kMegaCd.cheats.reset_on_mount
        && kPceCd.cheats.reset_on_mount,
    "the ONLY shipped CD family whose mount emits no index-255 "
    "cheat-reset bracket (megacd.cpp:175 / menu.cpp:2812 do)"
);

static_assert(
    kThreeDoSlots.size() == 1
        && kThreeDoSlots[0].role == SlotRole::Disc
        && kThreeDoSlots[0].index == kThreeDo.staging.disc_slot,
    "CdCore::mount_disc validates the disc against this table, so "
    "the disc row and the staging policy cannot disagree"
);
static_assert(
    kThreeDoSlots[0].extensions == std::string_view{"CUEISOCHD"},
    "3DO.sv S0,CUEISO plus menu.cpp:2477-2482 CHD append; 3docdd.cpp:316-337 loads all three"
);

static_assert(kThreeDo.staging.order == StageOrder::ThreeDo);
static_assert(
    kThreeDo.staging.save_dest == WideIoIndex{4},
    "3do.h:77 / 3do.cpp:80: SAVE_IO_INDEX 0x4, the empty-window "
    "download that TRIGGERS the save mount"
);
static_assert(
    kThreeDo.staging.save_slot == IoIndex{0},
    "3do.cpp:89: user_io_file_mount(buf, 0, 1)"
);
static_assert(
    kThreeDo.staging.reset_pulse_us == 0,
    "3do.cpp:130 asserts status bit 0 and :181 releases it: the bit "
    "is HELD across the ladder, so there is no pulse to time"
);
static_assert(
    kThreeDo.staging.save_name_disc_words.size() == 2
        && kThreeDo.staging.save_name_disc_words[0] == std::string_view{"disc"}
        && kThreeDo.staging.save_name_disc_words[1] == std::string_view{"disk"},
    "3do.cpp:61-62: `disc` is searched before `disk`, and the "
    "search ORDER decides which run is cut from a name holding both"
);

static_assert(
    kThreeDoBootAssets.size() == 5,
    "3do.cpp:135-158: three BIOS candidates then two kanji ones"
);
static_assert(
    kThreeDoBootAssets[0].anchor == AssetAnchor::ImageDir
        && kThreeDoBootAssets[1].anchor == AssetAnchor::ImageParentDir
        && kThreeDoBootAssets[2].anchor == AssetAnchor::Search,
    "3do.cpp:135-146: image dir, then its parent, then HomeDir()"
);
static_assert(
    kThreeDoBootAssets[2].name == std::string_view{"boot.rom"}
        && kThreeDoBootAssets[2].subdir == std::string_view{"3DO"},
    "3do.cpp:141: the home-folder candidate is boot.rom, NOT "
    "cd_bios.rom, and HomeDir() is games/<core name>"
);
static_assert(
    kThreeDoBootAssets[3].dest == WideIoIndex{3} && kThreeDoBootAssets[4].dest == WideIoIndex{3},
    "3do.cpp:152,:155: p3do_load_rom(..., 3) — a RAW io index, not "
    "a `sub << 6` sub-index"
);
static_assert(
    kThreeDoBootAssets[0].group == 0
        && kThreeDoBootAssets[1].group == 0
        && kThreeDoBootAssets[2].group == 0
        && kThreeDoBootAssets[3].group == 1
        && kThreeDoBootAssets[4].group == 1,
    "3do.cpp:151: the kanji stage is a SECOND alternatives group, "
    "run only when the first one hit"
);
static_assert(
    !kThreeDoBootAssets[0].group_required && !kThreeDoBootAssets[3].group_required,
    "3do.cpp:139-144: a missing BIOS is a toast and the ladder "
    "CONTINUES; only the kanji stage is skipped"
);

static_assert(threedo_blank::kSaveBlank.size() == 256, "3do.cpp:221: memcpy(buf, save_blank, 256)");
static_assert(
    kThreeDoBlankSave.fill == std::byte{0x00},
    "3do.cpp:222,:226: everything the header does not name is zero"
);
static_assert(
    kThreeDoBlankSave.declared() && kThreeDoBlankSave.regions.empty(),
    "one image-offset-0 record and nothing else — never the generic "
    "0xFF fallback"
);
static_assert(
    threedo_blank::kSaveBlank[0] == std::byte{0x01}
        && threedo_blank::kSaveBlank[6] == std::byte{0x02},
    "3do.cpp:199: the record opens 01 5A 5A 5A 5A 5A 02"
);
static_assert(
    threedo_blank::kSaveBlank[40] == std::byte{'N'}
        && threedo_blank::kSaveBlank[41] == std::byte{'V'}
        && threedo_blank::kSaveBlank[44] == std::byte{'M'},
    "3do.cpp:201: the ASCII volume label \"NVRAM\" at +40"
);

consteval int threedo_blank_nonzero_bytes() {
    int n = 0;
    for (std::byte b : threedo_blank::kSaveBlank) {
        if (b != std::byte{0})
            ++n;
    }
    return n;
}

static_assert(
    threedo_blank_nonzero_bytes() == 41,
    "3do.cpp:198-216: 41 nonzero bytes; a check joins the "
    "whole 256 against the classic's own `save_blank` object"
);

static_assert(
    kThreeDoCuePolicy.iso_sizing == svc::CuePolicy::IsoSizing::Declared2048,
    "3docdd.cpp:315-390: LoadISO asserts 2048/TT_MODE1 and reads no "
    "volume descriptor — the one classic CD engine with an ISO arm"
);
static_assert(
    kThreeDoCuePolicy.sub_naming == svc::CuePolicy::SubNaming::None,
    "3docdd.cpp:59-291: this parser opens no `.sub` sidecar"
);
static_assert(
    !kThreeDoCuePolicy.wav_header_skip && !kThreeDoCuePolicy.sniff_sega_disc_system,
    "both sniffs are other families': the .wav one PCE-CD's, the "
    "SEGADISCSYSTEM one MegaCD's"
);
static_assert(
    kThreeDoCuePolicy.track_close == svc::CuePolicy::TrackClose::SentinelPregapRelative,
    "3docdd.cpp:226 closes at the accumulated pregap and :237 at "
    "`start - pregap`; MegaCD/PCE-CD close at the INDEX 00 position"
);
static_assert(
    kThreeDoCuePolicy.offset_op == svc::CuePolicy::OffsetOp::Accumulate,
    "3docdd.cpp:230 is `offset += pregap * 2352`, a HARDCODED 2352; "
    "the running-size multiply is Saturn's, not this family's"
);
static_assert(
    svc::unimplemented_axis(kThreeDoCuePolicy) == svc::CueAxis::None,
    "3DO rides the Console program: only the container axis and the "
    "track close move, and both name arms DiscEngine has"
);

std::unique_ptr<Core> make_threedo(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<CdCore>(p, kThreeDoProfile, g.services);
}

std::unique_ptr<BootLadder> make_threedo_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<ThreeDoLadder>(p, ctx);
}

}
