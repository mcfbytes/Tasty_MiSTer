// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <memory>

#include "cores/control_slot.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/mac/mac_windows.h"
#include "cores/repulse_rule.h"
#include "cores/slot_servant_row.h"
#include "svc/storage_backend.h"

namespace mister::cores::manifests {

inline constexpr auto kMacLcControlSlots = std::to_array<ControlSlot>({
    {.slot = mac::kToolboxSlot,
     .root = ControlSlot::Root::SharedFolder,
     .subdir = "shared",
     .announce = ControlSlot::Announce::WhenDirectory},
    {.slot = mac::kChangerSlot,
     .root = ControlSlot::Root::CoreHome,
     .subdir = "CD3",
     .announce = ControlSlot::Announce::Always},
});

inline constexpr CoreProfile kMacLc{
    .kind = CoreKind::MacLc,
    .name = "MACLC",
    .rbf = "MacLC",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .signatures = {},

    .block_sector_floor = proto::SectorFloor::at(proto::BlockSize{svc::disc::kRawFrameBytes}),
    .undeclared_role = SlotRole::Save,
    .grants = {Grant::Servants},
    .lba_windows = mac::kMacWindows,
    .control_slots = kMacLcControlSlots,
    .repulse =
        {.slot = mac::kCdSlot, .after = std::chrono::seconds{60}, .unread_reads = ReadCount{1}},
};

std::unique_ptr<Core> make_maclc(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<svc::IStorageBackend> make_maclc_toolbox(const SlotServantTerms& t);
std::unique_ptr<svc::IStorageBackend> make_maclc_cd(const SlotServantTerms& t);
std::unique_ptr<svc::IStorageBackend> make_maclc_changer(const SlotServantTerms& t);

inline constexpr auto kMacLcSlotServants = std::to_array<SlotServantRow>({
    {.slot = mac::kToolboxSlot, .make = &make_maclc_toolbox},
    {.slot = mac::kCdSlot, .make = &make_maclc_cd},
    {.slot = mac::kChangerSlot, .make = &make_maclc_changer},
});

}
