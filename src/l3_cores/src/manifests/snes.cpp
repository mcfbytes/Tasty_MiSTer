// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/snes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include "cores/snes/msu_companion.h"
#include "cores/snes/msu_machine.h"
#include "cores/snes/msu_wire.h"
#include "cores/snes/sgb_core.h"
#include "cores/snes/snes_core.h"
#include "cores/snes/snes_loader.h"

namespace mister::cores::manifests {

using namespace std::chrono_literals;

std::unique_ptr<Core> make_snes(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<snes::SnesCore>(p, g.services);
}

std::unique_ptr<ILoader> make_snes_loader(const LoaderContext& ctx) {
    (void)ctx;
    return std::make_unique<snes::SnesLoader>();
}

std::unique_ptr<Core> make_sgb(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<snes::SgbCore>(p, g.services, kSgbMailbox);
}

std::unique_ptr<ICompanion> make_msu_companion(const svc::Vfs& vfs) {
    return std::make_unique<snes::MsuCompanion>(vfs);
}

std::unique_ptr<IMailboxServant> make_msu_servant(const ServantGrant& g) {
    return std::make_unique<snes::MsuMachine>(g.vfs);
}

static_assert(kSnes.kind == CoreKind::Snes);
static_assert(
    kSgbWindows[0].region.offset == kSnesWindows[0].region.offset
        && kSgbWindows[0].region.len == kSnesWindows[0].region.len
        && kSgbWindows[0].feeds_pcm == kSnesWindows[0].feeds_pcm,
    "the two rows carry one MSU-1 data window: MsuCompanion writes the data base "
    "through kSnesWindows while the walk places against the row's own"
);
static_assert(kSgb.kind == CoreKind::Sgb);
static_assert(
    kSgb.name == std::string_view{"SGB"},
    "SGB.sv:152: hps_io CONF_STR field 0 is \"SGB\", the name stock's "
    "is_sgb() compares (user_io.cpp:249-252)"
);
static_assert(
    kSgb.services.size() == 1
        && kSgb.services[0].impl == &kMailboxTick<kSgb>
        && kSgb.services[0].period == reactor::kEveryRound
        && kSgb.services[0].cause == kSnes.services[0].cause
        && kSgbMailbox.polls().data() == kSnesMailbox.polls().data()
        && kSgbMailbox.polls().size() == kSnesMailbox.polls().size(),
    "user_io.cpp:3223: snes_poll runs for is_snes() || is_sgb(), one cadence and frame"
);
static_assert(
    kSgb.undeclared_start_mount == StartMount::Unmodelled,
    "SgbCore forwards a mount to GenericCore, which arms none: no SC start mount"
);
static_assert(
    kSnes.name == std::string_view{"SNES"},
    "user_io.cpp:244: is_snes() is strcasecmp(orig_name, \"SNES\")"
);
static_assert(
    kSnes.services.size() == 1 && kSnes.services[0].period == reactor::kEveryRound,
    "user_io.cpp:3223: snes_poll runs on every user_io_poll pass, ungated"
);
static_assert(
    kSnesMailbox.polls().size() == 1
        && kSnesMailboxFrame.row().opcode == proto::word(proto::UioOp::CdGet)
        && kSnesMailboxFrame.kept() == proto::ReplyCount{4}
        && kSnesMailboxFrame.max_words() == proto::FioWordCount{4},
    "snes.cpp:536-541: spi_uio_cmd_cont(UIO_CD_GET), then three spi_w(0)"
);
static_assert(
    kSnesMailboxFrame.held_words() == proto::FioWordCount{1}
        && kSnesMailboxFrame.row().guard.reply == snes::msu::kCounterReply,
    "snes.cpp:542-545: an unmoved request byte ends the read after the opcode beat"
);
static_assert(
    snes::msu::kCdSet == 0x35
        && snes::msu::kSetEnable == 1
        && snes::msu::kSetTrackSize == 2
        && snes::msu::kSetDataBase == 3,
    "snes.cpp:471-473 + user_io.h:63: UIO_CD_SET with MSU_CD_SET/TRACK_MOUNTED/DATA_BASE"
);
static_assert(
    snes::msu::set_enable(true)
            == mister::proto::MailboxWords{hal::SpiWord{0x8001}, hal::SpiWord{}, hal::SpiWord{}}
        && snes::msu::set_enable(false)
            == mister::proto::MailboxWords{hal::SpiWord{0x0001}, hal::SpiWord{}, hal::SpiWord{}},
    "snes.cpp:528: (has_cd << 15) | MSU_CD_SET, low word first (snes.cpp:481-488)"
);
static_assert(
    snes::msu::set_data_base()
        == mister::proto::MailboxWords{hal::SpiWord{0x0003}, hal::SpiWord{}, hal::SpiWord{0x2300}},
    "snes.cpp:524 sent 0x20600000; the base is now the aperture + snes::msu::kDataBaseOffset"
);
static_assert(
    snes::msu::set_track_size(0x0001'2345u)
        == mister::proto::MailboxWords{hal::SpiWord{0x0002}, hal::SpiWord{0x2345}, hal::SpiWord{1}},
    "snes.cpp:558: (f_audio.size << 16) | MSU_AUDIO_TRACK_MOUNTED"
);
static_assert(
    snes::msu::kSectorSize == mister::proto::BlockSize{1024}
        && snes::msu::kAudioIndex == 2
        && snes::msu::kDataIndex == 3,
    "snes.cpp:477,492,568: 1024-byte sectors on index 2; snes.cpp:525: the data on 3"
);
static_assert(
    std::ranges::none_of(
        kSnes.slots,
        [](const FileSlot& s) { return s.index == proto::IoIndex{snes::msu::kDataIndex}; }
    ),
    "no content row loads on index 3: a window close there is the data track's"
);
static_assert(
    !kSnes.blank_save.declared(),
    "user_io.cpp:3474-3477: stock SNES has no blank-fill arm; a "
    "fresh save reads back the generic 0xFF memset"
);
static_assert(
    kSnes.slots.empty(),
    "SNES.sv: the power-on RAM image is the cartridge row's `f,RAM` file, no slot of its own"
);
static_assert(
    !kSnes.staging.staged,
    "no boot ladder: the save mount is the host-side opensave act "
    "inside the ROM tx, not a staging rung"
);

}
