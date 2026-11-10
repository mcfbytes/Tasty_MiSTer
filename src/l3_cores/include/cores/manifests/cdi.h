// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"
#include "cores/cdi_block.h"

#include <array>
#include <memory>

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "svc/cue_policy.h"

namespace mister::cores::manifests {

inline constexpr svc::CuePolicy kCdiCuePolicy{
    .sub_naming = svc::CuePolicy::SubNaming::ImageSubOrCdg,
    .program = svc::CuePolicy::Program::IndexPair,
    .frame_shaping = svc::CuePolicy::FrameShaping::DescrambleByHeader,
};

inline constexpr auto kCdiSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "CUECHD",
     .label = "Load CD",
     .writable = false,
     .required = false,
     .role = SlotRole::Disc},
});

inline constexpr auto kCdiStartAssets = stock_start_chain("CD-i");

inline constexpr StagingPolicy kCdiStaging{
    .order = StageOrder::Cdi,
    .reset_pulse_us = 0,
    .disc_slot = disc_slot_of(kCdiSlots),
    .save = SaveMountPolicy{
        .slot = IoIndex{1},
        .pre_bytes = proto::FileSize{8 * 1024},
        .pre_on_empty = true,
        .start_dir = "games/CD-i",
    },
};

inline constexpr CoreProfile kCdi{
    .kind = CoreKind::Cdi,
    .name = "CD-i",
    .rbf = "",
    .services = {},
    .slots = kCdiSlots,
    .boot_assets = {},
    .start_assets = kCdiStartAssets,
    .start = StartRule::Assets,
    .signatures = {},

    .blank_save = {.fill = std::byte{0xFF}, .header = {}, .regions = {}, .generic_ff = true},
    .staging = kCdiStaging,
    .block_drain_budget = 1,
    .block_sector_floor = proto::SectorFloor::at(proto::BlockSize{cdi::kBlockBytes}),
    .image_rows_no_zip = true,
    .cheats = {},
    .file_tx_whole = true,
    .reset = {},
};

std::unique_ptr<Core> make_cdi(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_cdi_ladder(const CoreProfile& p, const LadderContext& ctx);

}
