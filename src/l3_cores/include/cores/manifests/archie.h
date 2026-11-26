// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <string_view>

#include "cores/archie/archie_config_codec.h"
#include "cores/archie/archie_core.h"
#include "cores/archie/archie_ctrl.h"
#include "cores/archie/archie_option.h"
#include "cores/boot_ladder.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core.h"
#include "cores/input_sink_id.h"
#include "cores/option_row.h"
#include "proto/types.h"
#include "proto/user_io/poll_shape.h"

namespace mister::cores::manifests {

inline constexpr auto kArchieSlots = std::to_array<FileSlot>({
    {.index = archie::ArchieCore::kFloppy0Slot,
     .extensions = "ADF",
     .label = "Floppy 0",
     .writable = true},
    {.index = archie::ArchieCore::kFloppy1Slot,
     .extensions = "ADF",
     .label = "Floppy 1",
     .writable = true},
    {.index = archie::ArchieCore::kRomSlot,
     .extensions = "ROM",
     .label = "OS ROM",
     .writable = false,
     .role = SlotRole::Ladder},
});

inline constexpr auto kArchieAspectNames = std::to_array<std::string_view>(
    {"Original", "Full Screen", "[ARC1]", "[ARC2]"}
);
inline constexpr auto kArchieScaleNames = std::to_array<std::string_view>(
    {"Normal", "V-Integer", "HV-Integer-", "HV-Integer+"}
);
inline constexpr auto kArchieRefreshNames = std::to_array<std::string_view>({"60Hz", "Variable"});
inline constexpr auto kArchieStereoNames = std::to_array<std::string_view>(
    {"0%", "25%", "50%", "100%"}
);
inline constexpr auto kArchieAudioFixNames = std::to_array<std::string_view>({"Disable", "Enable"});
inline constexpr auto kArchieYesNo = std::to_array<std::string_view>({"No", "Yes"});

inline constexpr proto::SubmenuNo kArchiePage{0};

[[nodiscard]] consteval proto::BitField archie_field(archie::ArchieOption o) {
    return archie::SystemCtrl::home_of(o).value().status;
}

inline constexpr auto kArchieOptionRows = std::to_array<OptionRow>({
    OptionRow::file(
        " Floppy 0: ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::Floppy0),
        {.slot = archie::ArchieCore::kFloppy0Slot, .browse_unmount = true}
    ),
    OptionRow::file(
        " Floppy 1: ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::Floppy1),
        {.slot = archie::ArchieCore::kFloppy1Slot, .browse_unmount = true}
    ),
    OptionRow::separator(kArchiePage),
    OptionRow::file(
        " OS ROM: ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::OsRom),
        {.slot = archie::ArchieCore::kRomSlot}
    ),
    OptionRow::separator(kArchiePage),
    OptionRow::cycle(
        " Aspect Ratio:    ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::Aspect),
        {.bits = archie_field(archie::ArchieOption::Aspect), .values = kArchieAspectNames}
    ),
    OptionRow::cycle(
        " Scale:           ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::Scale),
        {.bits = archie_field(archie::ArchieOption::Scale), .values = kArchieScaleNames}
    ),
    OptionRow::cycle(
        " Refresh Rate:    ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::Refresh),
        {.bits = archie_field(archie::ArchieOption::Refresh), .values = kArchieRefreshNames}
    ),
    OptionRow::cycle(
        " Stereo Mix:      ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::StereoMix),
        {.bits = archie_field(archie::ArchieOption::StereoMix), .values = kArchieStereoNames}
    ),
    OptionRow::cycle(
        " 25MHz Audio Fix: ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::AudioFix),
        {.bits = archie_field(archie::ArchieOption::AudioFix), .values = kArchieAudioFixNames}
    ),
    OptionRow::host_fact(
        " Swap Joysticks:  ",
        kArchiePage,
        archie::tag_of(archie::ArchieOption::JoySwap),
        {.host = OptionRow::Host::JoySwap, .values = kArchieYesNo}
    ),
});

inline constexpr auto kArchieOptionPages = std::to_array<std::string_view>({""});

inline constexpr auto kArchiePolls = std::to_array<proto::user_io::PollShape>({
    proto::user_io::PollShape::ChangeCounter,
    proto::user_io::PollShape::KbdController,
});

inline constexpr CoreProfile kArchie{
    .kind = CoreKind::Archie,
    .name = "ARCHIE",
    .rbf = "Archie",
    .services = {},
    .slots = kArchieSlots,
    .boot_assets = {},
    .start = StartRule::None,
    .signatures = {},
    .suppresses_mgl = true,
    .option_rows = kArchieOptionRows,
    .option_pages = kArchieOptionPages,

    .no_stream_role = true,

    .reset = {.ladders = true, .restore_boots = true, .reload_hold = std::chrono::seconds{1}},
    .host = {.forces_gui_f12 = true, .input_sink = InputSinkId::KbdController},

    .config_codec = infra::OptRef<const IConfigCodec>{archie::kArchieConfigCodec},
    .settings_in_startup_slot = true,
    .polls = kArchiePolls,
    .cmos = infra::OptRef<const CmosLayout>{archie::ArchieCore::kCmos},
};

std::unique_ptr<Core> make_archie(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_archie_ladder(const CoreProfile& p, const LadderContext& ctx);

}
