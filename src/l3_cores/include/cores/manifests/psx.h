// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "cores/cd_census_rows.h"
#include "cores/cd_core.h"
#include "cores/core_profile.h"
#include "svc/disc_engine.h"

namespace mister::cores::manifests {

extern const CoreProfile kPsx;

consteval std::array<std::byte, 8192> psx_mcd_header_image() {
    std::array<std::byte, 8192> img{};
    constexpr std::size_t kFrame = 128;
    auto at = [&img](std::size_t frame, std::size_t off) -> std::byte& {
        return img[frame * kFrame + off];
    };

    for (std::size_t f : {std::size_t{0}, std::size_t{63}}) {
        at(f, 0) = std::byte{'M'};
        at(f, 1) = std::byte{'C'};
    }

    for (std::size_t f = 1; f <= 15; ++f) {
        at(f, 0) = std::byte{0xA0};
        at(f, 8) = std::byte{0xFF};
        at(f, 9) = std::byte{0xFF};
    }

    for (std::size_t f = 16; f <= 35; ++f) {
        for (std::size_t i = 0; i < 4; ++i)
            at(f, i) = std::byte{0xFF};
        at(f, 8) = std::byte{0xFF};
        at(f, 9) = std::byte{0xFF};
    }

    for (std::size_t f = 0; f < 64; ++f) {
        std::byte x{};
        for (std::size_t i = 0; i < kFrame - 1; ++i)
            x ^= at(f, i);
        at(f, kFrame - 1) = x;
    }
    return img;
}

inline constexpr std::array<std::byte, 8192> kPsxMcdHeader = psx_mcd_header_image();

inline constexpr BlankSaveSpec kPsxBlankSave{
    .fill = std::byte{0x00},
    .header = kPsxMcdHeader,
    .regions = {},
};

inline constexpr svc::CuePolicy kPsxCuePolicy{
    .sub_naming = svc::CuePolicy::SubNaming::None,
    .track_close = svc::CuePolicy::TrackClose::ProvisionalInclusive,
    .pregap_model = svc::CuePolicy::PregapModel::IndexTriple,
    .post_load_shift = svc::CuePolicy::PostLoadTocShift::FakeLeadInPregap150,
};

inline constexpr auto kPsxStartAssets = stock_start_chain("PSX");

inline constexpr auto kPsxBootAssets = std::to_array<BootAsset>({
    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageDir,
     .dest = WideIoIndex{0xC0},
     .group = 1,
     .exact_size = 512 * 1024},
    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageParentDir,
     .dest = WideIoIndex{0xC0},
     .group = 1,
     .exact_size = 512 * 1024},
});

inline constexpr auto kPsxSlots = std::to_array<FileSlot>({
    {.index = IoIndex{1},
     .extensions = "CUECHD",
     .label = "Load CD",
     .writable = false,
     .required = false,
     .role = SlotRole::Disc},
    {.index = IoIndex{2},
     .extensions = "SAVMCD",
     .label = "Mount Memory Card 1",
     .writable = true,
     .required = false,
     .role = SlotRole::Save},
    {.index = IoIndex{3},
     .extensions = "SAVMCD",
     .label = "Mount Memory Card 2",
     .writable = true,
     .required = false,
     .role = SlotRole::Save},
});

inline constexpr StagingPolicy kPsxStaging{
    .order = StageOrder::Psx,
    .reset_pulse_us = 0,
    .disc_slot = disc_slot_of(kPsxSlots),
    .save_slot = IoIndex{2},
    .save_dest = WideIoIndex{},
};

inline constexpr auto kPsxServices = std::to_array<LinkDecoderDecl>({
    {"psx.sector_kick", reactor::Cause::Tick, &kSectorKick<kPsx>, 0, DeadlineClass::A,
     OsdBudget::Shared},
});

inline constexpr const LinkDecoderDecl& kPsxSectorKick = kPsxServices[0];

inline constexpr CoreProfile kPsx{
    .kind = CoreKind::Psx,
    .name = "PSX",
    .rbf = "",
    .services = kPsxServices,
    .slots = kPsxSlots,
    .boot_assets = kPsxBootAssets,
    .start_assets = kPsxStartAssets,
    .signatures = {},
    .blank_save = kPsxBlankSave,
    .staging = kPsxStaging,
    .image_rows_no_zip = true,

    .cheats = {.reset_on_remount = true},
    .file_tx_whole = true,
};

std::unique_ptr<Core> make_psx(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_psx_ladder(const CoreProfile& p, const LadderContext& ctx);

}  // namespace mister::cores::manifests
