// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/st.h"

#include <string_view>

#include "cores/st/acsi_servant.h"
#include "cores/st/st_core.h"
#include "infra/indexed_array.h"
#include "cores/st/st_loader.h"
#include "cores/st/st_media_ladder.h"
#include "cores/st/st_sysctrl.h"
#include "infra/opt_ref.h"

namespace mister::cores::manifests {

using namespace std::chrono_literals;

namespace {

constexpr const char* kInquirySerial = TASTY_VDATE "  ";

}

std::unique_ptr<Core> make_st(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<st::StCore>(p, g.services);
}

std::unique_ptr<IDiskServant> make_st_disk_servant(const CoreProfile& p, const ServantGrant& g) {
    return std::make_unique<st::AcsiServant>(
        infra::OptRef<const svc::Vfs>{g.vfs},
        p.disk_formats,
        kInquirySerial,
        g.activity
    );
}

std::unique_ptr<BootLadder> make_st_ladder(const CoreProfile& p, const LadderContext& ctx) {
    return std::make_unique<st::StMediaLadder>(p, ctx);
}

std::unique_ptr<ILoader> make_st_loader(const LoaderContext&) {
    return std::make_unique<st::StLoader>();
}

static_assert(kAtariSt.kind == CoreKind::AtariSt);
static_assert(
    kAtariSt.name == std::string_view{"AtariST"},
    "user_io.cpp:385-388: is_st() is strcasecmp(orig_name, \"AtariST\"), a tri-state "
    "latch reset by user_io_read_core_name (user_io.cpp:462)"
);
static_assert(
    kAtariSt.rbf == std::string_view{"AtariST"},
    "HomeDir() is games/<core_name>, and core_name IS the CONF_STR name here"
);

static_assert(
    kAtariSt.suppress_reset_status_write,
    "req / user_io.cpp:592: user_io_status_set NEVER emits the generic "
    "128-bit UIO_SET_STATUS2 burst for an ST core, at any call site, for the whole "
    "session; user_io.cpp:1706 suppresses the reset release with it"
);
static_assert(
    kAtariSt.suppresses_mgl,
    "req / user_io.cpp:1761: an MGL playlist aimed at an ST core is "
    "force-completed (`mgl_get()->done = 1`) without executing a single item, in the "
    "same predicate that covers the menu, archie and SharpMZ"
);
static_assert(
    kAtariSt.default_uart_baud == BaudRate{19200},
    "user_io.cpp:1726: `uart_speeds[0] = is_st() ? 19200 : 115200` seeds an empty "
    "CONF_STR speed list, and 19200 is the ST's MIDI-era default"
);

static_assert(
    kAtariSt.services.size() == 1,
    "user_io.cpp:3222: tos_poll() is the ST's ONE per-pass obligation, and its whole "
    "body is get_dmastate() plus the button long-press (owed, item)"
);
static_assert(kAtariSt.services[0].name == std::string_view{"st.acsi_poll"});
static_assert(
    kAtariSt.services[0].cause == reactor::Cause::Tick,
    "st_tos.cpp:381-389 polls the 16-byte DMA state block; the fabric raises no "
    "doorbell for it today (polling-census.md:62 names it a BLK candidate)"
);
static_assert(
    kAtariSt.services[0].round_claim == RoundClaim::Always,
    "the ACSI poll claims each round it runs (no OSD row)"
);
static_assert(
    kAtariSt.services[0].period == 1ms,
    "the ticker FALLBACK cadence, not the stock rate: stock polls every user_io "
    "pass and 1 ms is the executive's floor"
);
static_assert(
    kAtariSt.services[0].impl == &kSectorKick<kAtariSt>,
    "identity is the PROFILE ADDRESS: a foreign core bound to this row answers "
    "nothing (cd_census_rows.h's own guard, reused)"
);

static_assert(
    kAtariSt.slots.size() == 6,
    "st_tos.cpp:527-554 dispatches tos_insert_disk on index <= 1 (floppies) versus "
    "2/3 (ACSI), and menu.cpp:4701/:4730/:4929/:4959 select TOS and cartridge "
    "through two more browser continuations"
);
static_assert(
    kAtariSt.slots[0].index == IoIndex{0} && kAtariSt.slots[1].index == IoIndex{1},
    "st_tos.cpp:531: floppies A and B are media indices 0 and 1"
);
static_assert(
    kAtariSt.slots[0].extensions == std::string_view{"ST "},
    "menu.cpp:4701: strcpy(fs_pFileExt, \"ST\"), packed to the frozen 3-char field"
);
static_assert(
    kAtariSt.slots[2].index == IoIndex{2} && kAtariSt.slots[3].index == IoIndex{3},
    "st_tos.cpp:553: index & 1 selects hdd_image[], so 2 and 3 are ACSI 0 and 1"
);
static_assert(
    kAtariSt.slots[2].extensions == std::string_view{"VHD"},
    "menu.cpp:4929: the HDD continuation browses VHD"
);
static_assert(
    kAtariSt.slots[2].writable && kAtariSt.slots[3].writable,
    "st_tos.cpp:517: FileOpenEx(..., O_RDWR | O_SYNC) — the ACSI image is written "
    "through, and the O_SYNC is why IDiskImage::write_at flushes"
);
static_assert(
    kAtariSt.slots[4].extensions == std::string_view{"IMG"},
    "menu.cpp:4959: SelectFile(..., \"IMG\", ...) then tos_upload(selPath)"
);
static_assert(
    kAtariSt.slots[5].extensions == std::string_view{"STC"},
    "menu.cpp:4730: strcpy(fs_pFileExt, \"STC\") then tos_load_cartridge(selPath)"
);
static_assert(
    !kAtariSt.slots[4].writable && !kAtariSt.slots[5].writable,
    "the TOS ROM and the cartridge are read-only sources; neither is ever written"
);

static_assert(
    kAtariSt.disk_formats.size() == 1,
    "one catch-all row: the ACSI image has no magic, no size rule and no extension "
    "rule, because stock sniffs none of the three"
);
static_assert(
    kAtariSt.disk_formats[0].codec == svc::DiskCodec::Raw,
    "st_tos.cpp:241-253: sector N is byte offset N*512 of the file, nothing else"
);
static_assert(
    kAtariSt.disk_formats[0].ext.empty()
        && kAtariSt.disk_formats[0].size_bytes == 0
        && kAtariSt.disk_formats[0].magic.empty(),
    "a catch-all row must carry NO rule of any kind, or select_disk_format's first "
    "two passes could reject the image stock would have opened"
);

static_assert(
    !kAtariSt.staging.staged,
    "st_tos.cpp:556-591 is the ST's media ladder, not a CD staging order: the "
    "16 KiB clear, the TOS, the ACSI remount and the cartridge, in that order"
);
static_assert(
    role_of(kAtariSt, IoIndex{0}) == SlotRole::Image
        && role_of(kAtariSt, IoIndex{2}) == SlotRole::Ladder
        && role_of(kAtariSt, IoIndex{3}) == SlotRole::Ladder
        && role_of(kAtariSt, IoIndex{4}) == SlotRole::Ladder
        && role_of(kAtariSt, IoIndex{5}) == SlotRole::Ladder,
    "st_tos.cpp:556-591 re-reads the ACSI, TOS and cartridge paths on every reset: "
    "MAIN's ladder records each mount at slots 2..5; a floppy is a plain mount"
);
static_assert(
    kAtariSt.reset.ladders
        && kAtariSt.reset.action_reacts
        && kAtariSt.reset.restore_boots
        && kAtariSt.reset.button_reacts,
    "menu.cpp:4784/:4792 and :5051 reset through tos_reset, and tos_config_load's "
    "caller cold-boots (st_tos.cpp:593-597); the USER release runs tos_reset(0) "
    "(user_io.cpp:3077)"
);
static_assert(
    !kAtariSt.blank_save.declared(),
    "no staging ladder means no deferred-create save slot to fill"
);
static_assert(
    kAtariSt.boot_assets.empty(),
    "the TOS path is CONFIG (tos_config_t::tos_img), not a search cascade"
);
static_assert(
    kAtariSt.signatures.empty(),
    "nothing about the ST is decided by a file's magic bytes"
);
static_assert(
    kAtariSt.init_additions.empty(),
    "the ST adds no class-H fabric register beyond the shared requirement set"
);
static_assert(
    !kAtariSt.host.front_end,
    "user_io.cpp:385: is_st() and is_menu() are different latches"
);
static_assert(kAtariSt.host.cue_browse_dir.empty(), "menu.cpp:455's PCECD_DIR swap is PCE-only");
static_assert(
    !kAtariSt.host.analog_reshape && !kAtariSt.host.suppress_analog_followup,
    "input.cpp has ZERO ST code: the IKBD is in the FPGA"
);
static_assert(
    !kAtariSt.host.cheats.cd_asset_layout,
    "the ST declares no cheat channel; findGameAsset's CD layout is a CD fact"
);

static_assert(
    st::st_ctrl::kDefaultSystemCtrl == 0x20000102u,
    "st_tos.cpp:617: TOS_MEMCONFIG_1M | TOS_CONTROL_VIDEO_COLOR | TOS_CONTROL_BORDER"
);
static_assert(st::st_ctrl::kCpuReset == 0x00000001u, "st_tos.h:33 — bit 0 is the CPU reset");
static_assert(
    st::st_ctrl::kAcsi0Enable == 0x00000400u,
    "st_tos.h:40 — TOS_ACSI0_ENABLE, and `<< i` is the per-target enable"
);
static_assert(
    st::st_ctrl::write_protect_bit(0) == 0x00000040u
        && st::st_ctrl::write_protect_bit(1) == 0x00000080u,
    "st_tos.h:34-35 — the floppy A/B write-protect bits (req)"
);

static_assert(
    st::StCore::kClearIndex == IoIndex{3} && st::StCore::kClearBytes == 16u * 1024u,
    "st_tos.cpp:565 — fill_tx(0, 16*1024, 3) clears the first 16 KiB"
);
static_assert(
    st::StLoader::kTosLargeBytes == proto::FileSize{256u * 1024u}
        && st::StLoader::kTosSmallBytes == proto::FileSize{192u * 1024u},
    "st_tos.cpp:571-573 — TOS dispatch is purely by SIZE"
);
static_assert(
    st::StLoader::kTosLargeIndex == proto::WideIoIndex{0}
        && st::StLoader::kTosSmallIndex == proto::WideIoIndex{1},
    "st_tos.cpp:571-573 — >= 256 KiB goes to index 0, exactly 192 KiB to index 1"
);
static_assert(
    st::StLoader::kCartIndex == proto::WideIoIndex{2}
        && st::StLoader::kCartBufferBytes == proto::FileSize{(128u * 1024u) + 4u},
    "st_tos.cpp:417-427 — a 128 KiB + 4 byte buffer, index 2, first 4 bytes dropped"
);

static_assert(
    kStOptionRows.size() == 35 && kStOptionRows.size() <= kMaxOptionRows,
    "menu.cpp:4627-4661 writes 15 rows before Exit and :4813-4897 writes 20 before "
    "Back, separators included; Exit and Back are the page frame, not rows"
);

namespace {
constexpr std::size_t st_row(st::StOption o) noexcept {
    for (std::size_t i = 0; i < kStOptionRows.size(); ++i)
        if (kStOptionRows[i].id == st::tag_of(o)
            && kStOptionRows[i].kind() != OptionRow::Kind::Separator)
            return i;
    return kStOptionRows.size();
}

constexpr const OptionRow::CycleArm& cycle_of(st::StOption o) noexcept {
    return *kStOptionRows[st_row(o)].cycle_arm();
}

constexpr const OptionRow::FileArm& file_of(st::StOption o) noexcept {
    return *kStOptionRows[st_row(o)].file_arm();
}

constexpr const OptionRow::LinkArm& link_of(st::StOption o) noexcept {
    return *kStOptionRows[st_row(o)].link_arm();
}

constexpr const OptionRow::HostArm& host_of(st::StOption o) noexcept {
    return *kStOptionRows[st_row(o)].host_arm();
}

constexpr std::size_t st_rows_on(proto::SubmenuNo page) noexcept {
    std::size_t n = 0;
    for (const auto& r : kStOptionRows)
        if (r.page == page)
            ++n;
    return n;
}
}

static_assert(
    st_rows_on(kStMainPage) == 15 && st_rows_on(kStConfigPage) == 20,
    "menu.cpp:4629 menumask 0xeff = 11 selectable rows + Exit, and 4 separators; "
    ":4814 menumask 0x1fffb = 15 selectable rows + Back, and 5 separators"
);
static_assert(
    st_row(st::StOption::FloppyA) == 0
        && st_row(st::StOption::FloppyB) == 1
        && st_row(st::StOption::WriteProtect) == 2
        && st_row(st::StOption::Cartridge) == 4
        && st_row(st::StOption::JoySwap) == 6
        && st_row(st::StOption::ModifyConfig) == 8
        && st_row(st::StOption::LoadConfig) == 9
        && st_row(st::StOption::SaveConfig) == 10
        && st_row(st::StOption::Mt32Pi) == 11
        && st_row(st::StOption::Reset) == 13
        && st_row(st::StOption::ColdBoot) == 14,
    "menu.cpp:4634-4661: the main page's row ORDER"
);
static_assert(
    st_row(st::StOption::Hdd0) == 15
        && st_row(st::StOption::Hdd1) == 16
        && st_row(st::StOption::Memory) == 18
        && st_row(st::StOption::Tos) == 19
        && st_row(st::StOption::Chipset) == 20
        && st_row(st::StOption::Blitter) == 22
        && st_row(st::StOption::Viking) == 23
        && st_row(st::StOption::Aspect) == 24
        && st_row(st::StOption::Screen) == 25
        && st_row(st::StOption::Mono60) == 26
        && st_row(st::StOption::VideoCrop) == 27
        && st_row(st::StOption::Scale) == 28
        && st_row(st::StOption::Scanlines) == 29
        && st_row(st::StOption::Dongle) == 31
        && st_row(st::StOption::YmAudio) == 33,
    "menu.cpp:4824-4895: the config page's row ORDER"
);
static_assert(
    cycle_of(st::StOption::WriteProtect).bits == proto::BitField{proto::StatusBit{6}, 2}
        && cycle_of(st::StOption::WriteProtect).values.size() == 4,
    "menu.cpp:4640,:4712-4714: (ctrl >> 6) & 3 over config_tos_wrprot[4]"
);
static_assert(
    cycle_of(st::StOption::Memory).bits == proto::BitField{proto::StatusBit{1}, 3}
        && cycle_of(st::StOption::Memory).choice_count() == proto::FieldCount{6}
        && cycle_of(st::StOption::Memory).values.size() == 8
        && cycle_of(st::StOption::Memory).arms_reset,
    "menu.cpp:4831,:4950-4953: tos_mem[(ctrl >> 1) & 7] shows eight names, a select "
    "wraps at 5, and the change arms need_reset"
);
static_assert(
    cycle_of(st::StOption::Chipset).bits == proto::BitField{proto::StatusBit{23}, 2},
    "menu.cpp:4840,:4965: bits 24:23, STE | MSTE"
);
static_assert(
    !cycle_of(st::StOption::Blitter).bits.valid()
        && !cycle_of(st::StOption::Viking).bits.valid()
        && !cycle_of(st::StOption::Aspect).bits.valid()
        && !cycle_of(st::StOption::VideoCrop).bits.valid(),
    "menu.cpp:4845-4859,:4870-4872: four rows whose display is derived from more "
    "than one field, answered by st::StCore itself"
);
static_assert(
    cycle_of(st::StOption::Screen).bits == proto::BitField{proto::StatusBit{8}, 1}
        && cycle_of(st::StOption::Mono60).bits == proto::BitField{proto::StatusBit{30}, 1}
        && cycle_of(st::StOption::Scanlines).bits == proto::BitField{proto::StatusBit{20}, 2}
        && cycle_of(st::StOption::Dongle).bits == proto::BitField{proto::StatusBit{25}, 1}
        && cycle_of(st::StOption::YmAudio).bits == proto::BitField{proto::StatusBit{22}, 1},
    "st_tos.h:37,:69,:57-59,:63,:56 — VIDEO_COLOR, MDE60, SCANLINES, DONGLE, STEREO"
);
static_assert(
    cycle_of(st::StOption::Scale).word == kStExtCtrl
        && cycle_of(st::StOption::Scale).bits == proto::BitField{proto::StatusBit{11}, 2}
        && cycle_of(st::StOption::Scale).values.size() == 4,
    "menu.cpp:4876,:5020-5021: config_scale[(ext >> 11) & 3], the ONLY ext_ctrl row"
);
static_assert(
    file_of(st::StOption::FloppyA).slot == IoIndex{0}
        && file_of(st::StOption::FloppyB).slot == IoIndex{1}
        && file_of(st::StOption::Cartridge).slot == IoIndex{5}
        && file_of(st::StOption::Hdd0).slot == IoIndex{2}
        && file_of(st::StOption::Hdd1).slot == IoIndex{3}
        && file_of(st::StOption::Tos).slot == IoIndex{4},
    "the File rows name the manifest slots kAtariStSlots declares"
);
static_assert(
    file_of(st::StOption::FloppyA).ejects_on_select
        && file_of(st::StOption::Cartridge).ejects_on_select
        && !file_of(st::StOption::Hdd0).ejects_on_select
        && !file_of(st::StOption::Tos).ejects_on_select,
    "menu.cpp:4691-4694,:4720-4723: an inserted floppy or cartridge ejects on select; "
    ":4926 browses an HDD with SCANO_UMOUNT; :4959 browses the TOS"
);
static_assert(
    link_of(st::StOption::ModifyConfig).link == OptionRow::Link::SubPage
        && link_of(st::StOption::ModifyConfig).link_page == kStConfigPage
        && link_of(st::StOption::LoadConfig).link == OptionRow::Link::LoadConfig
        && link_of(st::StOption::SaveConfig).link == OptionRow::Link::SaveConfig
        && link_of(st::StOption::Mt32Pi).link == OptionRow::Link::Mt32Pi,
    "menu.cpp:4748-4776: the four links; :4658 shows MT32-pi only under OSD mask bit 0"
);
static_assert(
    host_of(st::StOption::JoySwap).host == OptionRow::Host::JoySwap,
    "menu.cpp:4648,:4740: user_io_get_joyswap is the HOST's, not the ST's"
);
static_assert(
    kStOptionPages.size() == 2
        && infra::at_index(kStOptionPages, kStMainPage).empty()
        && infra::at_index(kStOptionPages, kStConfigPage) == "Config",
    "menu.cpp:4630 titles the main page with the core's own name, :4815 the sub-page "
    "'Config'; an empty entry means the core name"
);
static_assert(
    file_of(st::StOption::Hdd0).browse_unmount
        && file_of(st::StOption::Hdd1).browse_unmount
        && !file_of(st::StOption::Tos).browse_unmount
        && !file_of(st::StOption::FloppyA).browse_unmount,
    "menu.cpp:4927 is the ONLY ST browse opened with SCANO_UMOUNT"
);
static_assert(
    st::st_ctrl::kExtCrop216 == 0x400u
        && st::st_ctrl::kSte == 0x00800000u
        && st::st_ctrl::kMemConfig8M == (4u << 1),
    "menu.cpp:4871 (ext & 0x400), st_tos.h:60 TOS_CONTROL_STE, :22 TOS_MEMCONFIG_8M"
);

}
