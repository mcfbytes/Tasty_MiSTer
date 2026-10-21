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
#include "cores/st_options.h"
#include "proto/bit_field.h"
#include "svc/disk_codec.h"
#include "svc/disk_format_row.h"

namespace mister::cores::manifests {

extern const CoreProfile kAtariSt;

inline constexpr auto kAtariStServices = std::to_array<LinkDecoderDecl>({
    {
        "st.acsi_poll",
        reactor::Cause::Tick,
        &kSectorKick<kAtariSt>,
        1,
        DeadlineClass::A,
        OsdBudget::Pinned,
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
inline constexpr std::uint8_t kStMainPage = 0;
inline constexpr std::uint8_t kStConfigPage = 1;

[[nodiscard]] constexpr std::uint8_t st_id(StOption o) noexcept {
    return static_cast<std::uint8_t>(o);
}

[[nodiscard]] constexpr proto::BitField st_bits(std::uint8_t start, std::uint8_t width) noexcept {
    return proto::BitField{proto::StatusBit{start}, width};
}

inline constexpr auto kStOptionRows = std::to_array<OptionRow>({

    OptionRow::file(" A: ", kStMainPage, st_id(StOption::FloppyA), IoIndex{0}, true),
    OptionRow::file(" B: ", kStMainPage, st_id(StOption::FloppyB), IoIndex{1}, true),
    OptionRow::cycle(
        " Write protect:  ",
        kStMainPage,
        st_id(StOption::WriteProtect),
        kStSystemCtrl,
        st_bits(6, 2),
        kStWriteProtectNames
    ),
    OptionRow::separator(kStMainPage),
    OptionRow::file(" Cart: ", kStMainPage, st_id(StOption::Cartridge), IoIndex{5}, true),
    OptionRow::separator(kStMainPage),
    OptionRow::host_fact(
        " Joysticks swap: ",
        kStMainPage,
        st_id(StOption::JoySwap),
        OptionRow::Host::JoySwap,
        kStYesNo
    ),
    OptionRow::separator(kStMainPage),
    OptionRow::to(
        " Modify config",
        kStMainPage,
        st_id(StOption::ModifyConfig),
        OptionRow::Link::SubPage,
        kStConfigPage
    ),
    OptionRow::to(
        " Load config",
        kStMainPage,
        st_id(StOption::LoadConfig),
        OptionRow::Link::LoadConfig
    ),
    OptionRow::to(
        " Save config",
        kStMainPage,
        st_id(StOption::SaveConfig),
        OptionRow::Link::SaveConfig
    ),
    OptionRow::to(" MT32-pi", kStMainPage, st_id(StOption::Mt32Pi), OptionRow::Link::Mt32Pi),
    OptionRow::separator(kStMainPage),
    OptionRow::action(" Reset", kStMainPage, st_id(StOption::Reset), OptionRow::ResetAct::Warm),
    OptionRow::action(
        " Cold Boot",
        kStMainPage,
        st_id(StOption::ColdBoot),
        OptionRow::ResetAct::Cold
    ),

    OptionRow::file(" HDD0: ", kStConfigPage, st_id(StOption::Hdd0), IoIndex{2}, false, true),
    OptionRow::file(" HDD1: ", kStConfigPage, st_id(StOption::Hdd1), IoIndex{3}, false, true),
    OptionRow::separator(kStConfigPage),
    OptionRow::cycle(
        " Memory:     ",
        kStConfigPage,
        st_id(StOption::Memory),
        kStSystemCtrl,
        st_bits(1, 3),
        kStMemoryNames,
        6,
        true
    ),
    OptionRow::file(" TOS:        ", kStConfigPage, st_id(StOption::Tos), IoIndex{4}, false),
    OptionRow::cycle(
        " Chipset:    ",
        kStConfigPage,
        st_id(StOption::Chipset),
        kStSystemCtrl,
        st_bits(23, 2),
        kStChipsetNames
    ),
    OptionRow::separator(kStConfigPage),
    OptionRow::cycle(
        " Blitter:    ",
        kStConfigPage,
        st_id(StOption::Blitter),
        kStSystemCtrl,
        {},
        kStOnOff
    ),
    OptionRow::
        cycle(" Viking:     ", kStConfigPage, st_id(StOption::Viking), kStSystemCtrl, {}, kStOnOff),
    OptionRow::cycle(
        " Aspect:     ",
        kStConfigPage,
        st_id(StOption::Aspect),
        kStSystemCtrl,
        {},
        kStAspectNames
    ),
    OptionRow::cycle(
        " Screen:     ",
        kStConfigPage,
        st_id(StOption::Screen),
        kStSystemCtrl,
        st_bits(8, 1),
        kStScreenNames
    ),
    OptionRow::cycle(
        " Mono 60Hz:  ",
        kStConfigPage,
        st_id(StOption::Mono60),
        kStSystemCtrl,
        st_bits(30, 1),
        kStOnOff
    ),
    OptionRow::cycle(
        " Video Crop: ",
        kStConfigPage,
        st_id(StOption::VideoCrop),
        kStSystemCtrl,
        {},
        kStVideoCropNames
    ),
    OptionRow::cycle(
        " Scale:      ",
        kStConfigPage,
        st_id(StOption::Scale),
        kStExtCtrl,
        st_bits(11, 2),
        kStScaleNames
    ),
    OptionRow::cycle(
        " Scanlines:  ",
        kStConfigPage,
        st_id(StOption::Scanlines),
        kStSystemCtrl,
        st_bits(20, 2),
        kStScanlineNames
    ),
    OptionRow::separator(kStConfigPage),
    OptionRow::cycle(
        " Dongle:     ",
        kStConfigPage,
        st_id(StOption::Dongle),
        kStSystemCtrl,
        st_bits(25, 1),
        kStDongleNames
    ),
    OptionRow::separator(kStConfigPage),
    OptionRow::cycle(
        " YM-Audio:   ",
        kStConfigPage,
        st_id(StOption::YmAudio),
        kStSystemCtrl,
        st_bits(22, 1),
        kStStereoNames
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
    .default_uart_baud = 19200,
    .disk_formats = kAtariStDiskFormats,
    .option_rows = kStOptionRows,
    .option_pages = kStOptionPages,
    .file_tx_whole = true,

    .reset = {.button_reacts = true, .action_reacts = true, .ladders = true, .restore_boots = true},
};

std::unique_ptr<Core> make_st(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_st_ladder(const CoreProfile& p, const LadderContext& ctx);

}
