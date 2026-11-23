// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string_view>

#include "cores/core_profile.h"
#include "cores/core_window_decl.h"
#include "cores/disk_servant.h"
#include "cores/disk_servant_row.h"
#include "cores/file_slot.h"
#include "cores/host_bit_row.h"
#include "cores/ide/ide_geometry_rule.h"
#include "cores/ide/ide_image_geometry.h"
#include "cores/key_policy.h"
#include "cores/sector_kick_row.h"
#include "cores/share_host.h"
#include "cores/share_row.h"
#include "cores/x86/hdd_table_rule.h"
#include "cores/x86/x86_boot_loader.h"
#include "cores/x86/x86_bus_poll.h"
#include "cores/x86/x86_family.h"
#include "cores/x86/x86_slot.h"
#include "cores/x86/x86_sys_config_codec.h"
#include "svc/disk_format_row.h"

namespace mister::cores {
struct CoreGrant;
class BootLadder;
class ILoader;
struct LadderContext;
struct LoaderContext;
}

namespace mister::svc {
class Vfs;
}

namespace mister::cores::manifests {

extern const CoreProfile kAo486;
extern const CoreProfile kPcxt;
extern const CoreProfile kPcxtEga;
extern const CoreProfile kTandy1000;
extern const CoreProfile kPcjr;

inline constexpr auto kAo486Services = std::to_array<LinkDecoderDecl>({
    {"x86.bus_poll",
     reactor::Cause::Tick,
     &kSectorKick<kAo486>,
     std::chrono::milliseconds{1},
     RoundClaim::Always},
});
inline constexpr auto kPcxtServices = std::to_array<LinkDecoderDecl>({
    {"x86.bus_poll",
     reactor::Cause::Tick,
     &kSectorKick<kPcxt>,
     std::chrono::milliseconds{1},
     RoundClaim::Always},
});
inline constexpr auto kPcxtEgaServices = std::to_array<LinkDecoderDecl>({
    {"x86.bus_poll",
     reactor::Cause::Tick,
     &kSectorKick<kPcxtEga>,
     std::chrono::milliseconds{1},
     RoundClaim::Always},
});
inline constexpr auto kTandy1000Services = std::to_array<LinkDecoderDecl>({
    {"x86.bus_poll",
     reactor::Cause::Tick,
     &kSectorKick<kTandy1000>,
     std::chrono::milliseconds{1},
     RoundClaim::Always},
});
inline constexpr auto kPcjrServices = std::to_array<LinkDecoderDecl>({
    {"x86.bus_poll",
     reactor::Cause::Tick,
     &kSectorKick<kPcjr>,
     std::chrono::milliseconds{1},
     RoundClaim::Always},
});

inline constexpr auto kX86RawFormats = std::to_array<svc::DiskFormatRow>({
    {.ext = {},
     .size_bytes = 0,
     .tolerance = 0,
     .magic = {},
     .codec = svc::DiskCodec::Raw,
     .why = "ide.cpp:94-120 ide_img_mount: the floppy image opens raw, read-write when allowed"},
});

consteval FileSlot x86_slot(FileSlot row) {
    row.writable = true;
    row.role = SlotRole::Ladder;
    return row;
}

inline constexpr auto kAo486Slots = std::to_array<FileSlot>({
    x86_slot({.index = x86::kFloppyA.io_index(), .extensions = "IMGIMAVFD", .label = "Floppy A:"}),
    x86_slot({.index = x86::kFloppyB.io_index(), .extensions = "IMGIMAVFD", .label = "Floppy B:"}),
    x86_slot({.index = x86::kIde00.io_index(), .extensions = "VHD", .label = "IDE 0-0"}),
    x86_slot({.index = x86::kIde01.io_index(), .extensions = "VHD", .label = "IDE 0-1"}),
    x86_slot({.index = x86::kIde10.io_index(), .extensions = "VHDISOCUECHD", .label = "IDE 1-0"}),
    x86_slot({.index = x86::kIde11.io_index(), .extensions = "VHDISOCUECHD", .label = "IDE 1-1"}),
});
inline constexpr auto kPcxtSlots = std::to_array<FileSlot>({
    x86_slot({.index = x86::kFloppyA.io_index(), .extensions = "IMGIMAVFD", .label = "Floppy A:"}),
    x86_slot({.index = x86::kFloppyB.io_index(), .extensions = "IMGIMAVFD", .label = "Floppy B:"}),
    x86_slot({.index = x86::kIde00.io_index(), .extensions = "VHD", .label = "IDE 0-0"}),
    x86_slot({.index = x86::kIde01.io_index(), .extensions = "VHD", .label = "IDE 0-1"}),
});
inline constexpr auto kPcjrSlots = std::to_array<FileSlot>({
    x86_slot({.index = x86::kFloppyA.io_index(), .extensions = "IMGIMAVFD", .label = "Floppy A:"}),
});

inline constexpr auto kAo486Windows = std::to_array<CoreWindowDecl>({
    {.region = {0x1000'0000u, 0x0010'0000u, "x86-mem"}, .feeds_pcm = false},
});

inline constexpr ShareRow kAo486Share{
    .page = {.slot = x86::X86BootLoader::kGuestMemory, .offset = WindowOffset{0xCE000}},
    .bytes = proto::FileSize{0x2000},
    .poll = infra::ParkTimeout{1},
    .default_dir = "shared",
};

inline constexpr auto kAo486HostBits = std::to_array<HostBitRow>({
    {.fact = HostFact::RamSize, .bits = proto::BitField{proto::StatusBit{11}, 1}},
});

inline constexpr ide::HotSwapRow kAo486HotSwap{
    .master = ide::HotSwapBit{proto::ReplyMask{0x0100}, ide::HotSwapBit::AllowWhen::Clear},
    .slave = ide::HotSwapBit{proto::ReplyMask{0x0200}, ide::HotSwapBit::AllowWhen::Set}
};

consteval ide::IdeChannelRow ao486_ide1_channel() {
    ide::IdeChannelRow row = x86::kIde1Channel;
    row.mounts = ide::MountRule::DiscElseAbsent;
    row.hot_swap = kAo486HotSwap;
    return row;
}

inline constexpr auto kAo486Channels = std::to_array<ide::IdeChannelRow>(
    {x86::kIde0Channel, ao486_ide1_channel()}
);
static_assert(
    kAo486Channels[1].mounts == ide::MountRule::DiscElseAbsent,
    "ao486 IDE 1 takes a disc under every non-.vhd name"
);
static_assert(
    kAo486Channels[1].hot_swap.at(ide::IdeDriveSelect::Master).has_value()
        && kAo486Channels[1].hot_swap.at(ide::IdeDriveSelect::Master)->allow
            == ide::HotSwapBit::AllowWhen::Clear,
    "x86.cpp:554: IDE 1-0's hot-swap bit reads allowed when clear"
);
static_assert(
    !kAo486Channels[0].hot_swap.at(ide::IdeDriveSelect::Master).has_value(),
    "ao486 IDE 0 declares no hot-swap bit"
);
inline constexpr auto kPcxtChannels = std::to_array<ide::IdeChannelRow>({x86::kIde0Channel});
inline constexpr x86::X86Family kAo486Family{
    .poll = x86::kTwoChannelBusPoll,
    .drives = proto::DiskUnits{}.with(proto::DiskUnit{0}).with(proto::DiskUnit{1}),
    .ide = x86::X86IdeBus{.channels = kAo486Channels, .geometry = ide::kImageGeometry},
    .roms = x86::X86BootRoms::DdrShadow,
    .memory = x86::X86Memory::StatusRamSize
};
inline constexpr x86::X86Family kPcxtFamily{
    .poll = x86::kOneChannelBusPoll,
    .drives = proto::DiskUnits{}.with(proto::DiskUnit{0}).with(proto::DiskUnit{1}),
    .ide = x86::X86IdeBus{.channels = kPcxtChannels, .geometry = x86::kHddTableRule}
};
inline constexpr x86::X86Family kPcjrFamily{
    .poll = x86::kNoChannelBusPoll,
    .drives = proto::DiskUnits{}.with(proto::DiskUnit{0})
};

inline constexpr x86::X86SysConfigCodec kAo486SysConfig{"AO486sys.cfg"};
inline constexpr x86::X86SysConfigCodec kPcxtSysConfig{"PCXTsys.cfg"};
inline constexpr x86::X86SysConfigCodec kPcxtEgaSysConfig{"PCXT-EGAsys.cfg"};
inline constexpr x86::X86SysConfigCodec kTandy1000SysConfig{"Tandy1000sys.cfg"};
inline constexpr x86::X86SysConfigCodec kPcjrSysConfig{"PCjrsys.cfg"};

consteval CoreProfile x86_profile(
    CoreKind kind,
    std::string_view name,
    std::span<const LinkDecoderDecl> services,
    std::span<const FileSlot> slots,
    std::span<const HostBitRow> host_bits,
    std::span<const CoreWindowDecl> windows,
    const IConfigCodec& codec,
    HostPolicy host,
    std::optional<ShareRow> share = std::nullopt
) {
    host.forces_gui_f12 = true;
    return CoreProfile{
        .kind = kind,
        .name = name,
        .rbf = "",
        .services = services,
        .slots = slots,
        .boot_assets = {},
        .start = StartRule::None,
        .signatures = {},
        .block_service = BlockService::CoreOwn,
        .disk_formats = kX86RawFormats,
        .windows = windows,
        .host_bits = host_bits,

        .reset = {.toggle_reacts = true, .button_reacts = true, .ladders = true, .reinits = true},
        .undeclared_start_mount = StartMount::Unmodelled,
        .host = host,
        .config_codec = infra::OptRef<const IConfigCodec>{codec},

        .grants = {Grant::Disk},
        .share = share,
    };
}

consteval CoreProfile x86_generic_profile(CoreKind kind, std::string_view name) {
    return CoreProfile{
        .kind = kind,
        .name = name,
        .rbf = "",
        .services = {},
        .slots = {},
        .boot_assets = {},
        .start = StartRule::None,
        .signatures = {},
        .block_service = BlockService::CoreOwn,
        .undeclared_start_mount = StartMount::Unmodelled,
        .host = {.image_rows_no_zip = true, .forces_gui_f12 = true},
    };
}

inline constexpr CoreProfile kPc110 = x86_generic_profile(CoreKind::Pc110, "PC110");
inline constexpr CoreProfile kZ486 = x86_generic_profile(CoreKind::Z486, "Z486");
inline constexpr CoreProfile kZ386 = x86_generic_profile(CoreKind::Z386, "Z386");

std::unique_ptr<Core> make_ao486(const CoreProfile& p, const CoreGrant& g);
std::unique_ptr<Core> make_pcxt(const CoreProfile& p, const CoreGrant& g);
std::unique_ptr<Core> make_pcjr(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<IDiskServant> make_ao486_bus(const CoreProfile& p, const ServantGrant& g);
std::unique_ptr<IDiskServant> make_pcxt_bus(const CoreProfile& p, const ServantGrant& g);

std::unique_ptr<IDiskServant> make_pcjr_fdc(const CoreProfile& p, const ServantGrant& g);

std::unique_ptr<IShareHost> make_ao486_share_host(const svc::Vfs& vfs, std::string_view root);

std::unique_ptr<BootLadder> make_ao486_ladder(const CoreProfile& p, const LadderContext& ctx);
std::unique_ptr<BootLadder> make_pcxt_ladder(const CoreProfile& p, const LadderContext& ctx);
std::unique_ptr<BootLadder> make_pcjr_ladder(const CoreProfile& p, const LadderContext& ctx);

std::unique_ptr<ILoader> make_ao486_loader(const LoaderContext& ctx);

inline constexpr auto kAo486DiskServants = std::to_array<DiskServantRow>({
    {.protocol = proto::DiskProtocol::IsaBus, .make = &make_ao486_bus},
});
inline constexpr auto kPcxtDiskServants = std::to_array<DiskServantRow>({
    {.protocol = proto::DiskProtocol::IsaBus, .make = &make_pcxt_bus},
});
inline constexpr auto kPcjrDiskServants = std::to_array<DiskServantRow>({
    {.protocol = proto::DiskProtocol::IsaBus, .make = &make_pcjr_fdc},
});

inline constexpr CoreProfile kAo486 = x86_profile(
    CoreKind::Ao486,
    "AO486",
    kAo486Services,
    kAo486Slots,
    kAo486HostBits,
    kAo486Windows,
    kAo486SysConfig,
    {.image_rows_no_zip = true},
    kAo486Share
);
inline constexpr CoreProfile kPcxt = x86_profile(
    CoreKind::Pcxt,
    "PCXT",
    kPcxtServices,
    kPcxtSlots,
    {},
    {},
    kPcxtSysConfig,
    {.keys = {.gui_chords_own = true}}
);
inline constexpr CoreProfile kPcxtEga = x86_profile(
    CoreKind::PcxtEga,
    "PCXT-EGA",
    kPcxtEgaServices,
    kPcxtSlots,
    {},
    {},
    kPcxtEgaSysConfig,
    {.keys = {.gui_chords_own = true}}
);
inline constexpr CoreProfile kTandy1000 = x86_profile(
    CoreKind::Tandy1000,
    "Tandy1000",
    kTandy1000Services,
    kPcxtSlots,
    {},
    {},
    kTandy1000SysConfig,
    {.keys = {.gui_chords_own = true}}
);
inline constexpr CoreProfile kPcjr = x86_profile(
    CoreKind::Pcjr,
    "PCjr",
    kPcjrServices,
    kPcjrSlots,
    {},
    {},
    kPcjrSysConfig,
    {.keys = {.gui_chords_own = true}}
);

}
