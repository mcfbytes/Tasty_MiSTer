// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_ladder.h"
#include "cores/manifests/cdi.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cores/cdi_block.h"
#include "cores/cdi_core.h"

namespace mister::cores::manifests {

std::unique_ptr<Core> make_cdi(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<CdiCore>(p, kCdiCuePolicy, g.services);
}

std::unique_ptr<BootLadder> make_cdi_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<CdiLadder>(p, ctx);
}

static_assert(kCdi.kind == CoreKind::Cdi);
static_assert(
    kCdi.name == std::string_view{"CD-i"},
    "user_io.cpp:380: is_cdi() is strcasecmp(orig_name, \"CD-i\")"
);
static_assert(kCdi.services.empty(), "cdi.cpp:1167: cdi_poll() is empty, so nothing is polled");
static_assert(
    kCdi.slots.size() == 1
        && kCdi.slots[0].index == IoIndex{0}
        && kCdi.slots[0].extensions == std::string_view{"CUECHD"}
        && kCdi.slots[0].role == SlotRole::Disc
        && !kCdi.slots[0].writable,
    "CDi.sv CONF_STR: S0,CUECHD,Load CD; user_io.cpp:3253 disk 0 is the CD"
);
static_assert(
    kCdi.staging.disc_slot == IoIndex{0} && kCdi.staging.save_slot == IoIndex{1},
    "cdi.cpp:492-497: the save is index 1; cdi.cpp:1152-1153 the disc index 0"
);
static_assert(
    kCdi.staging.save_pre_bytes == 8 * 1024,
    "cdi.cpp:25,497: TIMEKEEPER_SIZE (8 * 1024) is the pre_size"
);
static_assert(
    kCdi.staging.save_pre_on_empty,
    "CDi.sv:322 never writes a 0-size timekeeper: an empty file gets the pre-size too"
);
static_assert(
    kCdi.staging.start_save_dir == std::string_view{"games/CD-i"},
    "stock cdi_load_root_nvram: its last_dir"
);
static_assert(
    kCdi.staging.save_dest == WideIoIndex{},
    "cdi.cpp:497: a plain user_io_file_mount, no download bracket"
);
static_assert(
    !kCdi.blank_save.declared() && kCdi.blank_save.generic_ff,
    "user_io.cpp:3476: no CD-i blank arm; a fresh save reads memset(-1)"
);
static_assert(
    kCdi.image_rows_no_zip,
    "menu.cpp:2475/:2494: every CD-i S row gains CHD and loses zip"
);
static_assert(
    !kCdi.reset.toggle_reacts && !kCdi.reset.button_reacts && !kCdi.reset.toggle_ejects,
    "user_io.cpp:3070-3078, menu.cpp:2591-2600: no CD-i arm in either reset path"
);

consteval bool cdi_start_chain_is_stock() {
    constexpr std::string_view kNames[] =
        {"boot0.rom", "boot1.rom", "boot2.rom", "boot3.rom", "boot.rom"};
    constexpr std::uint8_t kDest[] = {0x00, 0x40, 0x80, 0xC0, 0x00};
    if (kCdi.start_assets.size() != 5)
        return false;
    for (std::size_t i = 0; i < 5; ++i) {
        const BootAsset& r = kCdi.start_assets[i];
        if (r.name != kNames[i] || r.dest.v != kDest[i] || r.subdir != kCdi.name)
            return false;
    }
    return true;
}

static_assert(
    cdi_start_chain_is_stock(),
    "user_io.cpp:1612-1625: boot0..3.rom at i << 6 under HomeDir(), then boot.rom at 0"
);
static_assert(
    svc::unimplemented_axis(kCdiCuePolicy) == svc::CueAxis::None,
    "cdi.cpp:145-367: the IndexPair program is built"
);
static_assert(
    kCdiCuePolicy.frame_shaping == svc::CuePolicy::FrameShaping::DescrambleByHeader,
    "cdi.cpp:1066: check_scramble runs on every track sector read"
);

static_assert(cdi::kBlockBytes == 2568 && cdi::kSubcodeBytes == 216);

static_assert(
    cdi::hunk_window(0) == 0
    && cdi::hunk_window(8 * 2448) == 0
    && cdi::hunk_window(4 * 2448) == 3
    && cdi::hunk_window(16384) == 6
);

consteval unsigned crc_sum() {
    unsigned s = 0;
    for (auto w : cdi::kCrcTable)
        s += w;
    return s;
}

static_assert(
    cdi::kCrcTable[1] == 0x1021 && cdi::kCrcTable[255] == 0x1EF0 && crc_sum() == 8388480,
    "cdi.cpp:519-552: every entry of s_crc_ccitt_table"
);

}
