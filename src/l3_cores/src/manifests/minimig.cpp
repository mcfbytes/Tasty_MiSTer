// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/minimig.h"

#include <string_view>

#include "cores/minimig/akiko_wire.h"
#include "cores/minimig/cd_bridge.h"
#include "cores/minimig/cd_decl.h"
#include "cores/minimig/cd_drives.h"
#include "cores/minimig/cd_status.h"
#include "cores/minimig/cr511_wire.h"
#include "cores/minimig/floppy_servant.h"
#include "cores/minimig/minimig_ladder.h"
#include "cores/minimig/share_host.h"
#include "proto/disk_protocol_row.h"
#include "svc/adf_codec.h"

namespace mister::cores::manifests {

std::unique_ptr<Core> make_minimig(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<minimig::MinimigCore>(p, g.services);
}

std::unique_ptr<IMailboxServant> make_minimig_cd_servant(const ServantGrant& g) {
    return std::make_unique<minimig::CdDrives>(g.vfs);
}

std::unique_ptr<BootLadder> make_minimig_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<minimig::MinimigLadder>(p, ctx);
}

std::unique_ptr<IShareHost> make_minimig_share_host(const svc::Vfs& vfs, std::string_view root) {
    return std::make_unique<minimig::ShareHost>(vfs, root);
}

std::unique_ptr<IDiskServant> make_minimig_floppy_servant(
    const CoreProfile& p,
    const ServantGrant& g
) {
    return std::make_unique<minimig::FloppyServant>(
        infra::OptRef<const svc::Vfs>{g.vfs},
        p.disk_formats,
        g.activity
    );
}

static_assert(kMinimig.kind == CoreKind::Minimig);
static_assert(
    kMinimig.name == std::string_view{"Minimig"},
    "user_io.cpp:281-295: is_minimig() is strcasecmp(orig_name, \"minimig\")"
);
static_assert(
    kMinimig.home_dir == std::string_view{"Amiga"},
    "user_io.cpp:200: a minimig session's home directory is Amiga, never its name"
);

static_assert(
    kMinimig.start == StartRule::None,
    "user_io.cpp:1577-1582: the start arm is BootInit, never the boot-file chain"
);
static_assert(
    kMinimig.suppress_reset_status_write,
    "user_io.cpp:1541: the saved status word is neither loaded nor re-asserted; "
    "user_io.cpp:1706: the reset release burst is not sent"
);
static_assert(
    kMinimig.reset.ladders && kMinimig.reset.restore_boots,
    "minimig_config.cpp:503-622: a config load ends in ApplyConfiguration(1); "
    "minimig_config.cpp:624-632: a reset is ApplyConfiguration(0)"
);
static_assert(
    kMinimig.reset.button_reacts,
    "user_io.cpp:3070 and :4207: the user button and the reset combo run minimig_reset"
);
static_assert(
    kMinimig.rtc_refresh == std::chrono::seconds{60},
    "user_io.cpp:3199-3204: the clock is re-sent once a minute"
);

static_assert(kMinimig.config_codec.has_value());
static_assert(
    minimig::mm_cfg::kBytes == infra::ByteCount{7270}
        && minimig::mm_cfg::kOldBytes == infra::ByteCount{3100},
    "minimig_config.h:49-70, minimig_config.cpp:31-47: the two file forms"
);
static_assert(minimig::mm_cfg::kSlotCount == 10, "menu.cpp:6135: the load page walks ten slots");

static_assert(kMinimigSlots.size() == 6, "four floppy drives, then the kickstart and the ext. ROM");
static_assert(
    kMinimigSlots[4].index == minimig::MinimigCore::kKickstartSlot
        && kMinimigSlots[5].index == minimig::MinimigCore::kExtRomSlot
        && kMinimigSlots[4].role == SlotRole::Ladder
        && kMinimigSlots[5].role == SlotRole::Ladder,
    "minimig_config.cpp:634-656: a pick only records the path and owes a reload"
);
static_assert(
    kMinimigSlots[0].index == IoIndex{0}
        && kMinimigSlots[3].index == IoIndex{3}
        && kMinimigSlots[0].extensions == std::string_view{"ADF"},
    "menu.cpp:5986-5991: df0..df3 browse ADF; menu.cpp:6115: InsertFloppy(&df[menusub])"
);
static_assert(
    kMinimigSlots[0].writable && kMinimigSlots[3].writable,
    "minimig_fdd.cpp:632-635: a writable file opens O_RDWR | O_SYNC"
);
static_assert(
    minimig::MinimigCore::kFloppyDrives == minimig::FloppyServant::kDrives
        && proto::disk_protocol_row(proto::DiskProtocol::AmigaFloppy)
               .units.contains(proto::DiskUnit{minimig::FloppyServant::kDrives - 1})
        && !proto::disk_protocol_row(proto::DiskProtocol::AmigaFloppy)
                .units.contains(proto::DiskUnit{minimig::FloppyServant::kDrives}),
    "minimig_fdd.cpp:38: adfTYPE df[4]; the protocol row addresses the same four"
);

static_assert(
    kMinimig.grants.has(Grant::Disk)
        && kMinimigDiskServants.size() == 1
        && kMinimigDiskServants[0].protocol == proto::DiskProtocol::AmigaFloppy,
    "user_io.cpp:3172-3190: HandleFDD and UpdateDriveStatus on every pass"
);
static_assert(
    kMinimig.disk_formats.size() == 1
        && kMinimig.disk_formats[0].codec == svc::DiskCodec::Adf
        && kMinimig.disk_formats[0].ext.empty()
        && kMinimig.disk_formats[0].magic.empty(),
    "minimig_fdd.cpp:630-648: InsertFloppy sniffs nothing; the file's size is its tracks"
);
static_assert(
    svc::AdfCodec::kTrackBytes == proto::FileSize{12668}
        && svc::AdfCodec::kSectorBytes == proto::FileSize{1088}
        && svc::AdfCodec::kGapBytes == proto::FileSize{700},
    "minimig_fdd.cpp:44-50: TRACK_SIZE 12668, SECTOR_SIZE 0x440, GAP_SIZE the rest"
);
static_assert(
    svc::AdfCodec::kMostTracks == svc::AdfCodec::TrackCount{166},
    "minimig_fdd.h:12: MAX_TRACKS (83*2)"
);
static_assert(
    kMinimig.services.size() == 2 && kMinimig.services[0].impl == &kSectorKick<kMinimig>,
    "identity is the profile address: a foreign core bound to this row answers nothing"
);
static_assert(kMinimig.no_stream_role, "no file is streamed: every upload is the ladder's frames");

static_assert(
    kMinimig.share.has_value()
        && kMinimig.share->page == WindowSpot{}
        && kMinimig.share->bytes == kMinimigShareBytes
        && kMinimig.share->poll == infra::ParkTimeout{1}
        && kMinimig.share->default_dir == minimig::kShareDefaultDir
        && kMinimig.share->page.holds(kMinimig.windows, kMinimig.share->bytes),
    "minimig_share.cpp:22-28: an 8 KB page at the window's start, looked at every "
    "user_io pass"
);

namespace cdb = minimig::cd_bridge;
static_assert(
    kMinimig.services[1].impl == &kMailboxTick<kMinimig>
        && kMinimig.services[1].period == reactor::kEveryRound,
    "user_io.cpp:3191-3195: both drive polls run on every user_io pass"
);
static_assert(kMinimig.grants.has(Grant::Mailbox));
static_assert(
    cdb::kStatus == hal::SpiWord{0x63}
        && cdb::kRead == hal::SpiWord{0x62}
        && proto::word(cdb::kWriteOp) == hal::SpiWord{0x61},
    "user_io.h:87-89: UIO_DMA_WRITE, UIO_DMA_READ, UIO_DMA_SDIO"
);
static_assert(
    cdb::kAkikoCommand == proto::DiskAddress{0xF400}
        && cdb::kAkikoSubcode == proto::DiskAddress{0xF410}
        && cdb::kAkikoNvram == proto::DiskAddress{0xF440}
        && cdb::kAkikoSector == proto::DiskAddress{0xF500}
        && cdb::kCdda == proto::DiskAddress{0xF200},
    "akiko_cd32.cpp:34-55: the CD32 bridge's chip selects"
);
static_assert(
    cdb::kCdtvCommand == proto::DiskAddress{0xF800}
        && cdb::kCdtvSector == proto::DiskAddress{0xF820}
        && cdb::kCdtvStch == proto::DiskAddress{0xF840}
        && cdb::kCdtvNvram == proto::DiskAddress{0xF880}
        && cdb::kCdtvCard == proto::DiskAddress{0xF8C0},
    "cdtv_cd.cpp:44-61: the CDTV bridge's chip selects"
);
static_assert(
    minimig::CdStatus::kAkikoRequest == proto::ReplyMask{1u << 11}
        && minimig::CdStatus::kAkikoSectorRequest == proto::ReplyMask{1u << 10}
        && minimig::CdStatus::kAkikoRxBusy == proto::ReplyMask{1u << 9}
        && minimig::CdStatus::kCddaRequest == proto::ReplyMask{1u << 8}
        && minimig::CdStatus::kAkikoNvramDirty == proto::ReplyMask{1u << 7}
        && minimig::CdStatus::kCdtvRequest == proto::ReplyMask{1u << 6}
        && minimig::CdStatus::kCdtvNvramDirty == proto::ReplyMask{1u << 12}
        && minimig::CdStatus::kCdtvCardDirty == proto::ReplyMask{1u << 13},
    "hps_ext.v 'h63 arm; akiko_cd32.cpp:40-47, cdtv_cd.cpp:49-61"
);
static_assert(
    minimig::akiko::payload_of(minimig::akiko::CommandByte{0x04}.opcode()) == proto::ReplyCount{12}
        && minimig::akiko::payload_of(minimig::akiko::CommandByte{0x08}.opcode())
            == proto::ReplyCount{4}
        && !minimig::akiko::payload_of(minimig::akiko::CommandByte{0x0B}.opcode())
        && minimig::akiko::payload_of(minimig::akiko::CommandByte{0x00}.opcode())
            == proto::ReplyCount{1},
    "akiko_cd32.cpp:63-65: command_lengths"
);
static_assert(
    minimig::cr511::frame_bytes(*minimig::cr511::op_of(std::byte{0x02})) == infra::ByteCount{7}
        && minimig::cr511::frame_bytes(*minimig::cr511::op_of(std::byte{0x81}))
            == infra::ByteCount{1}
        && minimig::cr511::frame_bytes(*minimig::cr511::op_of(std::byte{0x80}))
            == infra::ByteCount{2}
        && !minimig::cr511::op_of(std::byte{0x03}),
    "cdtv_cd.cpp:63-76: cr511_command_length"
);
static_assert(
    minimig::akiko::kFrameMax == 32 && minimig::cr511::kFrameMax == 16,
    "AKIKO_CMD_MAX, CDTV_CMD_MAX"
);
static_assert(minimig::cd_decl::kCdMailbox.max_words() <= proto::kMailboxPollWords);

}
