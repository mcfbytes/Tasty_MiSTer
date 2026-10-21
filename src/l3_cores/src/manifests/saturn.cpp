// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/saturn.h"

#include <string_view>

#include "cores/manifests/threedo.h"
#include "cores/saturn_ladder.h"

namespace mister::cores::manifests {

static_assert(kSaturnIoIndices.data == IoIndex{8}, "saturn.h:117: CD_DATA_IO_INDEX == 0x8");
static_assert(kSaturnIoIndices.info == IoIndex{0xC}, "saturn.h:118: BOOT_IO_INDEX == 0xC");
static_assert(
    kSaturnIoIndices.cdda == IoIndex{} && kSaturnIoIndices.subcode == IoIndex{},
    "saturncdd.cpp:1363: CD-DA is a MODE WORD on the data index; no subcode slot"
);
static_assert(
    kSaturnIoIndices.data == kThreeDoIoIndices.data
        && kSaturnIoIndices.info == kThreeDoIoIndices.info,
    "the two families share an io map, and only as data"
);

static_assert(
    kSaturnProfile.dialect == CdDialect::SaturnRing,
    "spec 1.5.2.1: Saturn reads 6 command words and writes 6 status words"
);
static_assert(
    kSaturnProfile.crc_start == 0,
    "saturncdd.cpp:1116-1120: stat[11] is ~sum(stat[0..10]), no BCD checksum"
);
static_assert(kSaturnProfile.seek_fast_paths.empty(), "PCE-CD's table; Saturn has none");
static_assert(
    !kSaturnProfile.sniff_sega_disc_system,
    "saturncdd.cpp:369-376: the SEGADISCSYSTEM sniff is commented out"
);

static_assert(
    kSaturnTicks.speeds == 3
        && kSaturnTicks.speed_us[0] == 16'667
        && kSaturnTicks.speed_us[1] == 13'333
        && kSaturnTicks.speed_us[2] == 6'667,
    "sat.cpp:35,:29,:23 — 16.7, 13.3 and 6.6 ms, speed 0, 1 and 2"
);
static_assert(
    kSaturnTicks.idle_us == kSaturnTicks.speed_us[1],
    "saturncdd.cpp:717-976: every arm but read_pend sets time_mode 1"
);
static_assert(
    kSaturnTicks.caller_us == kSaturnServices[1].period_ms * 1000u,
    "caller_us must BE the service row's own period_ms"
);
static_assert(
    kSaturnTicks.caller_us < kSaturnTicks.speed_us[2],
    "the row must run faster than the fastest pass, or the gate aliases it"
);

static_assert(kSaturnServices.size() == 2, "ARCHITECTURE.md 3.4: Saturn has two census rows");
static_assert(
    kSaturnServices[0].cause == reactor::Cause::Ring
        && kSaturnServices[1].cause == reactor::Cause::Tick,
    "the CD_GET edge is a RING doorbell; sat.cpp:48's pass is a ticker"
);
static_assert(
    kSaturnServices[0].klass == DeadlineClass::A && kSaturnServices[1].klass == DeadlineClass::A,
    "3.4: both Saturn services are Class A (T-RT)"
);
static_assert(
    kSaturnServices[0].period_ms == 1 && kSaturnServices[1].period_ms == 1,
    "one row period quantises the pass; 1 ms lands stock's 7/7/6 dither"
);
static_assert(
    kSaturnServices[0].impl == &kCdCommandEdge<kSaturn>
        && kSaturnServices[1].impl == &kCdServiceTick<kSaturn>,
    "the SHARED CD obligations keyed to this profile"
);
static_assert(
    kSaturnServices[0].impl != kThreeDoServices[0].impl
        && kSaturnServices[1].impl != kThreeDoServices[1].impl,
    "sharing the SHELL must not share the IDENTITY"
);

static_assert(kSaturn.kind == CoreKind::Saturn);
static_assert(
    kSaturn.name == std::string_view{"Saturn"},
    "user_io.cpp:406: is_saturn() is strcasecmp(orig_name, \"Saturn\")"
);
static_assert(
    kSaturn.signatures.empty(),
    "support/saturn carries no region enum, byte or BIOS sniff: region is "
    "whatever boot.rom the user placed"
);
static_assert(
    !kSaturn.cheats.reset_on_mount && kSaturn.cheats.cd_dir_suffix.empty(),
    "sat.cpp:205 comments cheats_init out and cheats.cpp names no Saturn arm"
);
static_assert(
    kSaturn.reset.toggle_reacts && kSaturn.reset.button_reacts && !kSaturn.reset.toggle_ejects,
    "user_io.cpp:3074, menu.cpp:2599 reach saturn_reset(); nothing ejects"
);

static_assert(
    kSaturnSlots.size() == 2
        && kSaturnSlots[0].role == SlotRole::Disc
        && kSaturnSlots[0].index == kSaturn.staging.disc_slot,
    "menu.cpp:2826-2828: S0 is saturn_set_image's disc"
);
static_assert(
    kSaturnSlots[1].role == SlotRole::Save
        && kSaturnSlots[1].index == IoIndex{1}
        && kSaturnSlots[1].index == kSaturn.staging.save_slot,
    "menu.cpp:2473,:2830: S1 browses saves and mounts on the save slot"
);
static_assert(
    kSaturnSlots[1].opens_zip && !kSaturnSlots[0].opens_zip,
    "menu.cpp:2475: SCANO_SAVES is the one row not given SCANO_NOZIP"
);
static_assert(
    kSaturnSlots[0].extensions == std::string_view{"CUECHD"},
    "saturncdd.cpp:326-356: Load takes .cue and .chd and refuses the rest"
);

static_assert(kSaturn.staging.order == StageOrder::Saturn);
static_assert(
    kSaturn.staging.save_slot == IoIndex{1},
    "sat.cpp:133: user_io_file_mount(buf, 1, 1) — slot 1, not 3DO's 0"
);
static_assert(
    kSaturn.staging.save_dest == WideIoIndex{4},
    "saturn.h:119 / sat.cpp:119: SAVE_IO_INDEX 0x4 frames the save mount"
);
static_assert(
    kSaturn.staging.reset_pulse_us == 0,
    "sat.cpp:176 asserts bit 0 and :235 releases it: a HOLD, not a pulse"
);
static_assert(
    kSaturn.staging.save_name_disc_words.size() == 1
        && kSaturn.staging.save_name_disc_words[0] == std::string_view{"disc"},
    "sat.cpp:101: disc/Disc/DISC only — 3DO's 'disk' is not searched"
);

static_assert(
    kSaturnBootAssets.size() == 3
        && kSaturnBootAssets[0].anchor == AssetAnchor::ImageDir
        && kSaturnBootAssets[1].anchor == AssetAnchor::ImageParentDir
        && kSaturnBootAssets[2].anchor == AssetAnchor::Search,
    "sat.cpp:180-185: image dir, its parent, then HomeDir()"
);
static_assert(
    kSaturnBootAssets[0].name == std::string_view{"cd_bios.rom"}
        && kSaturnBootAssets[2].name == std::string_view{"boot.rom"}
        && kSaturnBootAssets[2].subdir == std::string_view{"Saturn"},
    "sat.cpp:180,:184: cd_bios.rom twice, then <HomeDir>/boot.rom"
);
static_assert(
    kSaturnBootAssets[0].dest == WideIoIndex{0} && kSaturnBootAssets[2].dest == WideIoIndex{0},
    "sat.cpp:150 `sub_index << 6` with sub 0, and :185's default index"
);
static_assert(
    !kSaturnBootAssets[0].group_required,
    "sat.cpp:187: a missing BIOS is a toast and the mount CONTINUES"
);

static_assert(
    saturn_blank::kRecord[1] == std::byte{'B'}
        && saturn_blank::kRecord[31] == std::byte{'t'}
        && saturn_blank::kRecord[97] == std::byte{'B'}
        && saturn_blank::kRecord[0] == std::byte{0},
    "sat.cpp:253-260: UTF-16BE \"BackUpRam Format\", four times"
);
static_assert(
    kSaturnBlankSave.fill == std::byte{0x00}
        && kSaturnBlankSave.regions.size() == 1
        && kSaturnBlankSave.regions[0].offset_bytes == 65'536,
    "sat.cpp:289: the record at LBA 0 AND LBA 128 of 512-byte blocks"
);

static_assert(
    kSaturn.windows.size() == 1
        && !kSaturn.windows[0].feeds_pcm
        && kSaturn.windows[0].region.len == 4u * 4096u,
    "saturncdd.cpp:1280: shmem_map(SHMEM_ADDR, 4096 * 4) — four slots"
);

static_assert(kSaturnQuirks.size() == 7, "sat.cpp:213-225: five TOC titles, two READ titles");

std::unique_ptr<Core> make_saturn(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<CdCore>(p, kSaturnProfile, g.services);
}

std::unique_ptr<BootLadder> make_saturn_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<SaturnLadder>(p, ctx);
}

}
