// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <string_view>

#include "cores/boot_ladder.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/option_row.h"
#include "cores/sector_kick_row.h"
#include "cores/st_config.h"
#include "cores/st_options.h"
#include "proto/bit_field.h"
#include "proto/types.h"
#include "svc/disk_codec.h"
#include "svc/disk_format_row.h"

namespace mister::cores::manifests {

extern const CoreProfile kAtariSt;

inline constexpr auto kAtariStServices = std::to_array<LinkDecoderDecl>({
    {
        "st.acsi_poll",
        reactor::Cause::Tick,
        &kSectorKick<kAtariSt>,
        std::chrono::milliseconds{1},
        RoundClaim::Always,
    },
});

inline constexpr auto kAtariStDiskFormats = std::to_array<svc::DiskFormatRow>({
    {.ext = {},
     .size_bytes = 0,
     .tolerance = 0,
     .magic = {},
     .codec = svc::DiskCodec::Raw,
     .why = "st_tos.cpp:504-525 (req): the ACSI image is opened raw, "
            "with no geometry and no format sniff"},
});

inline constexpr auto kAtariStSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0}, .extensions = "ST ", .label = "A:", .writable = true},
    {.index = IoIndex{1}, .extensions = "ST ", .label = "B:", .writable = true},
    {.index = IoIndex{2},
     .extensions = "VHD",
     .label = "ACSI 0",
     .writable = true,
     .role = SlotRole::Ladder},
    {.index = IoIndex{3},
     .extensions = "VHD",
     .label = "ACSI 1",
     .writable = true,
     .role = SlotRole::Ladder},
    {.index = IoIndex{4},
     .extensions = "IMG",
     .label = "TOS",
     .writable = false,
     .role = SlotRole::Ladder},
    {.index = IoIndex{5},
     .extensions = "STC",
     .label = "Cartridge",
     .writable = false,
     .role = SlotRole::Ladder},
});

inline constexpr auto kStWriteProtectNames = std::to_array<std::string_view>(
    {"None", "A:", "B:", "A: and B:"}
);
inline constexpr auto kStMemoryNames = std::to_array<std::string_view>(
    {"512kb", "1mb", "2mb", "4mb", "8mb", "14mb", "--", "--"}
);
inline constexpr auto kStChipsetNames = std::to_array<std::string_view>(
    {"ST", "STE", "MegaSTE", "STEroids"}
);
inline constexpr auto kStOnOff = std::to_array<std::string_view>({"Off", "On"});
inline constexpr auto kStAspectNames = std::to_array<std::string_view>(
    {"Original", "Full Screen", "[ARC1]", "[ARC2]"}
);
inline constexpr auto kStScreenNames = std::to_array<std::string_view>({"Mono", "Color"});
inline constexpr auto kStVideoCropNames = std::to_array<std::string_view>(
    {"Full", "Visible", "Visible 216p(5x)"}
);
inline constexpr auto kStScaleNames = std::to_array<std::string_view>(
    {"Normal", "V-Integer", "HV-Integer-", "HV-Integer+"}
);
inline constexpr auto kStScanlineNames = std::to_array<std::string_view>(
    {"Off", "25%", "50%", "75%"}
);
inline constexpr auto kStDongleNames = std::to_array<std::string_view>({"None", "Cubase"});
inline constexpr auto kStStereoNames = std::to_array<std::string_view>({"Mono", "Stereo"});
inline constexpr auto kStYesNo = std::to_array<std::string_view>({"No", "Yes"});

inline constexpr WordId kStSystemCtrl{0};
inline constexpr WordId kStExtCtrl{1};
inline constexpr proto::SubmenuNo kStMainPage{0};
inline constexpr proto::SubmenuNo kStConfigPage{1};

inline constexpr auto kStOptionRows = std::to_array<OptionRow>({

    OptionRow::file(
        " A: ",
        kStMainPage,
        tag_of(StOption::FloppyA),
        {.slot = IoIndex{0}, .ejects_on_select = true}
    ),
    OptionRow::file(
        " B: ",
        kStMainPage,
        tag_of(StOption::FloppyB),
        {.slot = IoIndex{1}, .ejects_on_select = true}
    ),
    OptionRow::cycle(
        " Write protect:  ",
        kStMainPage,
        tag_of(StOption::WriteProtect),
        {.word = kStSystemCtrl,
         .bits = proto::BitField{proto::StatusBit{6}, 2},
         .values = kStWriteProtectNames}
    ),
    OptionRow::separator(kStMainPage),
    OptionRow::file(
        " Cart: ",
        kStMainPage,
        tag_of(StOption::Cartridge),
        {.slot = IoIndex{5}, .ejects_on_select = true}
    ),
    OptionRow::separator(kStMainPage),
    OptionRow::host_fact(
        " Joysticks swap: ",
        kStMainPage,
        tag_of(StOption::JoySwap),
        {.host = OptionRow::Host::JoySwap, .values = kStYesNo}
    ),
    OptionRow::separator(kStMainPage),
    OptionRow::to(
        " Modify config",
        kStMainPage,
        tag_of(StOption::ModifyConfig),
        {.link = OptionRow::Link::SubPage, .link_page = kStConfigPage}
    ),
    OptionRow::to(
        " Load config",
        kStMainPage,
        tag_of(StOption::LoadConfig),
        {.link = OptionRow::Link::LoadConfig}
    ),
    OptionRow::to(
        " Save config",
        kStMainPage,
        tag_of(StOption::SaveConfig),
        {.link = OptionRow::Link::SaveConfig}
    ),
    OptionRow::to(
        " MT32-pi",
        kStMainPage,
        tag_of(StOption::Mt32Pi),
        {.link = OptionRow::Link::Mt32Pi}
    ),
    OptionRow::separator(kStMainPage),
    OptionRow::action(
        " Reset",
        kStMainPage,
        tag_of(StOption::Reset),
        {.reset_act = OptionRow::ResetAct::Warm}
    ),
    OptionRow::action(
        " Cold Boot",
        kStMainPage,
        tag_of(StOption::ColdBoot),
        {.reset_act = OptionRow::ResetAct::Cold}
    ),

    OptionRow::file(
        " HDD0: ",
        kStConfigPage,
        tag_of(StOption::Hdd0),
        {.slot = IoIndex{2}, .browse_unmount = true}
    ),
    OptionRow::file(
        " HDD1: ",
        kStConfigPage,
        tag_of(StOption::Hdd1),
        {.slot = IoIndex{3}, .browse_unmount = true}
    ),
    OptionRow::separator(kStConfigPage),
    OptionRow::cycle(
        " Memory:     ",
        kStConfigPage,
        tag_of(StOption::Memory),
        {.word = kStSystemCtrl,
         .bits = proto::BitField{proto::StatusBit{1}, 3},
         .values = kStMemoryNames,
         .choices = ChoiceCount{6},
         .arms_reset = true}
    ),
    OptionRow::file(" TOS:        ", kStConfigPage, tag_of(StOption::Tos), {.slot = IoIndex{4}}),
    OptionRow::cycle(
        " Chipset:    ",
        kStConfigPage,
        tag_of(StOption::Chipset),
        {.word = kStSystemCtrl,
         .bits = proto::BitField{proto::StatusBit{23}, 2},
         .values = kStChipsetNames}
    ),
    OptionRow::separator(kStConfigPage),
    OptionRow::cycle(
        " Blitter:    ",
        kStConfigPage,
        tag_of(StOption::Blitter),
        {.word = kStSystemCtrl, .values = kStOnOff}
    ),
    OptionRow::cycle(
        " Viking:     ",
        kStConfigPage,
        tag_of(StOption::Viking),
        {.word = kStSystemCtrl, .values = kStOnOff}
    ),
    OptionRow::cycle(
        " Aspect:     ",
        kStConfigPage,
        tag_of(StOption::Aspect),
        {.word = kStSystemCtrl, .values = kStAspectNames}
    ),
    OptionRow::cycle(
        " Screen:     ",
        kStConfigPage,
        tag_of(StOption::Screen),
        {.word = kStSystemCtrl,
         .bits = proto::BitField{proto::StatusBit{8}, 1},
         .values = kStScreenNames}
    ),
    OptionRow::cycle(
        " Mono 60Hz:  ",
        kStConfigPage,
        tag_of(StOption::Mono60),
        {.word = kStSystemCtrl,
         .bits = proto::BitField{proto::StatusBit{30}, 1},
         .values = kStOnOff}
    ),
    OptionRow::cycle(
        " Video Crop: ",
        kStConfigPage,
        tag_of(StOption::VideoCrop),
        {.word = kStSystemCtrl, .values = kStVideoCropNames}
    ),
    OptionRow::cycle(
        " Scale:      ",
        kStConfigPage,
        tag_of(StOption::Scale),
        {.word = kStExtCtrl,
         .bits = proto::BitField{proto::StatusBit{11}, 2},
         .values = kStScaleNames}
    ),
    OptionRow::cycle(
        " Scanlines:  ",
        kStConfigPage,
        tag_of(StOption::Scanlines),
        {.word = kStSystemCtrl,
         .bits = proto::BitField{proto::StatusBit{20}, 2},
         .values = kStScanlineNames}
    ),
    OptionRow::separator(kStConfigPage),
    OptionRow::cycle(
        " Dongle:     ",
        kStConfigPage,
        tag_of(StOption::Dongle),
        {.word = kStSystemCtrl,
         .bits = proto::BitField{proto::StatusBit{25}, 1},
         .values = kStDongleNames}
    ),
    OptionRow::separator(kStConfigPage),
    OptionRow::cycle(
        " YM-Audio:   ",
        kStConfigPage,
        tag_of(StOption::YmAudio),
        {.word = kStSystemCtrl,
         .bits = proto::BitField{proto::StatusBit{22}, 1},
         .values = kStStereoNames}
    ),
    OptionRow::separator(kStConfigPage),
});

inline constexpr auto kStOptionPages = std::to_array<std::string_view>({"", "Config"});

inline constexpr CoreProfile kAtariSt{
    .kind = CoreKind::AtariSt,
    .name = "AtariST",
    .rbf = "AtariST",
    .services = kAtariStServices,
    .slots = kAtariStSlots,
    .boot_assets = {},
    .start = StartRule::None,
    .signatures = {},

    .suppress_reset_status_write = true,
    .suppresses_mgl = true,
    .default_uart_baud = BaudRate{19200},
    .disk_formats = kAtariStDiskFormats,
    .option_rows = kStOptionRows,
    .option_pages = kStOptionPages,
    .file_tx_whole = true,

    .reset = {.button_reacts = true, .action_reacts = true, .ladders = true, .restore_boots = true},

    .config_codec = infra::OptRef<const IConfigCodec>{kStConfigCodec},
};

std::unique_ptr<Core> make_st(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_st_ladder(const CoreProfile& p, const LadderContext& ctx);

}
