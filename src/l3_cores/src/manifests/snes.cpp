// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/snes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include "cores/msu_companion.h"
#include "cores/msu_wire.h"
#include "cores/snes_core.h"

namespace mister::cores::manifests {

std::unique_ptr<Core> make_snes(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<SnesCore>(p, g.services);
}

std::unique_ptr<ICompanionLoad> make_snes_companion(const svc::Vfs& vfs) {
    return std::make_unique<MsuCompanion>(vfs);
}

static_assert(kSnes.kind == CoreKind::Snes);
static_assert(kSnes.name == std::string_view{"SNES"},
              "user_io.cpp:244: is_snes() is strcasecmp(orig_name, \"SNES\")");
static_assert(kSnes.services.size() == 1 && kSnes.services[0].period_ms == 0 &&
                  kSnes.services[0].klass == DeadlineClass::A,
              "user_io.cpp:3223: snes_poll runs on every user_io_poll pass, ungated");
static_assert(kSnesMailboxFrame.poll_opcode == msu::kCdGet && msu::kCdGet == 0x34 &&
                  kSnesMailboxFrame.words == msu::kFrameWords &&
                  proto::well_formed(kSnesMailboxFrame),
              "snes.cpp:536-541: spi_uio_cmd_cont(UIO_CD_GET), then three spi_w(0)");
static_assert(
    msu::kCdSet == 0x35 && msu::kSetEnable == 1 && msu::kSetTrackSize == 2 &&
        msu::kSetDataBase == 3,
    "snes.cpp:471-473 + user_io.h:63: UIO_CD_SET with MSU_CD_SET/TRACK_MOUNTED/DATA_BASE");
static_assert(msu::set_enable(true) == std::array<std::uint16_t, 3>{0x8001, 0, 0} &&
                  msu::set_enable(false) == std::array<std::uint16_t, 3>{0x0001, 0, 0},
              "snes.cpp:528: (has_cd << 15) | MSU_CD_SET, low word first (snes.cpp:481-488)");
static_assert(msu::set_data_base(0x2060'0000u) == std::array<std::uint16_t, 3>{0x0003, 0, 0x2060},
              "snes.cpp:524: (0x20600000ULL << 16) | MSU_DATA_BASE");
static_assert(msu::set_track_size(0x0001'2345u) == std::array<std::uint16_t, 3>{0x0002, 0x2345, 1},
              "snes.cpp:558: (f_audio.size << 16) | MSU_AUDIO_TRACK_MOUNTED");
static_assert(msu::kSectorBytes == 1024 && msu::kAudioIndex == 2 && msu::kDataIndex == 3,
              "snes.cpp:477,492,568: 1024-byte sectors on index 2; snes.cpp:525: the data on 3");
static_assert(msu::kGapAt + 0x2060'0000u == 0x2200'0000u && msu::kGapBytes == 0x80'0000u &&
                  msu::kGapAt % 0x40000u == 0,
              "user_io.cpp:2882,2888: +0x800000 at 0x22000000, tested per 256 KiB chunk start");
static_assert(msu::window_offset(msu::kGapAt - 1) == msu::kGapAt - 1 &&
              msu::window_offset(msu::kGapAt) == msu::kGapAt + msu::kGapBytes);
static_assert(kSnesWindows[0].region.offset + msu::kDataBound + msu::kGapBytes == 0x1F80'0000u &&
                  msu::kStockDataBound == 0x1F20'0000u,
              "SNES.sv:1411,1417: the data span ends where the save-state DDR at $3F80.0000 "
              "starts; stock's snes.cpp:522 bound overruns it");
static_assert(std::ranges::none_of(kSnes.slots,
                                   [](const FileSlot& s) { return s.index.v == msu::kDataIndex; }),
              "no content row loads on index 3: a window close there is the data track's");
static_assert(!kSnes.blank_save.declared(),
              "user_io.cpp:3474-3477: stock SNES has no blank-fill arm; a "
              "fresh save reads back the generic 0xFF memset");
static_assert(
    kSnes.slots.empty(),
    "SNES.sv: the power-on RAM image is the cartridge row's `f,RAM` file, no slot of its own");
static_assert(kSnes.staging.order == StageOrder::None,
              "no boot ladder: the save mount is the host-side opensave act "
              "inside the ROM tx, not a staging rung");

}  // namespace mister::cores::manifests
