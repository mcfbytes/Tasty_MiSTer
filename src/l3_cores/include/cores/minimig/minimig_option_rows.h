// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <string_view>

#include "cores/minimig/minimig_core.h"
#include "cores/minimig/minimig_option_names.h"
#include "cores/minimig/minimig_options.h"
#include "cores/option_row.h"
#include "proto/types.h"

namespace mister::cores::minimig {

inline constexpr proto::SubmenuNo kMainPage{0};
inline constexpr proto::SubmenuNo kSystemPage{1};
inline constexpr proto::SubmenuNo kDrivesPage{2};
inline constexpr proto::SubmenuNo kAvPage{3};
inline constexpr proto::SubmenuNo kPresetPage{4};

inline constexpr auto kMinimigOptionPages = std::to_array<std::string_view>(
    {"", "System", "Drives", "Audio & Video", "Load preset"}
);

[[nodiscard]] constexpr OptionRow minimig_preset(std::string_view label, MinimigOption o) noexcept {
    return OptionRow::action(label, kPresetPage, tag_of(o), {.core_act = true, .arms_reset = true});
}

[[nodiscard]] constexpr OptionRow minimig_cycle(
    std::string_view label,
    proto::SubmenuNo page,
    MinimigOption o,
    std::span<const std::string_view> values
) noexcept {
    return OptionRow::cycle(label, page, tag_of(o), {.values = values});
}

inline constexpr auto kMinimigOptionRows = std::to_array<OptionRow>({

    minimig_cycle(" Joystick Swap:          ", kMainPage, MinimigOption::JoySwap, kJoySwapNames),
    OptionRow::separator(kMainPage),
    OptionRow::to(
        " CD & HDD",
        kMainPage,
        OptionTag{},
        {.link = OptionRow::Link::SubPage, .link_page = kDrivesPage}
    ),
    OptionRow::to(
        " System",
        kMainPage,
        OptionTag{},
        {.link = OptionRow::Link::SubPage, .link_page = kSystemPage}
    ),
    OptionRow::to(
        " Audio & Video",
        kMainPage,
        OptionTag{},
        {.link = OptionRow::Link::SubPage, .link_page = kAvPage}
    ),
    OptionRow::to(" MT32-pi", kMainPage, OptionTag{}, {.link = OptionRow::Link::Mt32Pi}),
    OptionRow::separator(kMainPage),
    OptionRow::to(
        " Save configuration",
        kMainPage,
        OptionTag{},
        {.link = OptionRow::Link::SaveConfig}
    ),
    OptionRow::to(
        " Load configuration",
        kMainPage,
        OptionTag{},
        {.link = OptionRow::Link::LoadConfig}
    ),
    OptionRow::to(
        " Load preset",
        kMainPage,
        OptionTag{},
        {.link = OptionRow::Link::SubPage, .link_page = kPresetPage}
    ),
    OptionRow::separator(kMainPage),
    OptionRow::action(" Reset", kMainPage, OptionTag{}, {.reset_act = OptionRow::ResetAct::Cold}),

    minimig_cycle(" CPU      : ", kSystemPage, MinimigOption::Cpu, kCpuNames),
    minimig_cycle(" D-Cache  : ", kSystemPage, MinimigOption::DCache, kOffOnNames),
    OptionRow::separator(kSystemPage),
    minimig_cycle(" Chipset  : ", kSystemPage, MinimigOption::Chipset, kChipsetNames),
    minimig_cycle(" ChipRAM  : ", kSystemPage, MinimigOption::ChipRam, kChipRamNames),
    minimig_cycle(" FastRAM  : ", kSystemPage, MinimigOption::FastRam, kFastRamNames),
    minimig_cycle(" SlowRAM  : ", kSystemPage, MinimigOption::SlowRam, kSlowRamNames),
    OptionRow::separator(kSystemPage),
    OptionRow::file(
        " ROM      : ",
        kSystemPage,
        tag_of(MinimigOption::Kickstart),
        {.slot = MinimigCore::kKickstartSlot}
    ),
    OptionRow::file(
        " Ext.ROM  : ",
        kSystemPage,
        tag_of(MinimigOption::ExtRom),
        {.slot = MinimigCore::kExtRomSlot, .browse_unmount = true}
    ),
    minimig_cycle(" HRTmon   : ", kSystemPage, MinimigOption::Hrtmon, kEnabledNames),
    OptionRow::separator(kSystemPage),
    minimig_cycle(" Joystick : ", kSystemPage, MinimigOption::JoyMode, kJoyModeNames),
    minimig_cycle(" FDD Turbo: ", kSystemPage, MinimigOption::FloppyTurbo, kOffOnNames),

    OptionRow::separator(kDrivesPage),
    minimig_cycle(" IDE A600/A1200   : ", kDrivesPage, MinimigOption::IdeEnable, kEnabledNames),
    minimig_cycle(" Fast-IDE (68020) : ", kDrivesPage, MinimigOption::FastIde, kFastIdeNames),
    minimig_cycle(" \x1bPri. Master: ", kDrivesPage, MinimigOption::Unit0, kUnitNames),
    minimig_cycle(" \x1bPri.  Slave: ", kDrivesPage, MinimigOption::Unit1, kUnitNames),
    minimig_cycle(" \x1bSec. Master: ", kDrivesPage, MinimigOption::Unit2, kUnitNames),
    minimig_cycle(" \x1bSec.  Slave: ", kDrivesPage, MinimigOption::Unit3, kUnitNames),

    minimig_cycle(" TV Standard   : ", kAvPage, MinimigOption::TvStandard, kTvNames),
    OptionRow::separator(kAvPage),
    minimig_cycle(" Scandoubler FX: ", kAvPage, MinimigOption::Scandoubler, kScandoublerNames),
    minimig_cycle(" Video area by : ", kAvPage, MinimigOption::VideoArea, kVideoAreaNames),
    minimig_cycle(" Aspect Ratio  : ", kAvPage, MinimigOption::Aspect, kAspectNames),
    minimig_cycle(" Pixel Clock   : ", kAvPage, MinimigOption::PixelClock, kPixelClockNames),
    minimig_cycle(" Scaling       : ", kAvPage, MinimigOption::Scaling, kScalingNames),
    minimig_cycle(" RTG Upscaling : ", kAvPage, MinimigOption::RtgUpscale, kRtgNames),
    OptionRow::separator(kAvPage),
    minimig_cycle(" Stereo mix    : ", kAvPage, MinimigOption::StereoMix, kStereoNames),
    minimig_cycle(" Audio Filter  : ", kAvPage, MinimigOption::AudioFilter, kFilterNames),
    minimig_cycle(" Model         : ", kAvPage, MinimigOption::Model, kModelNames),
    minimig_cycle(" Paula Output  : ", kAvPage, MinimigOption::PaulaOutput, kPaulaNames),

    OptionRow::separator(kPresetPage),
    minimig_preset(" A500  OCS ChipRAM 512KB", MinimigOption::PresetA500),
    minimig_preset(" A600  ECS ChipRAM 1MB", MinimigOption::PresetA600),
    minimig_preset(" A1200 AGA ChipRAM 2MB", MinimigOption::PresetA1200),
});

}
