// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_grant.h"
#include "cores/boot_ladder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "cores/cd/cd_census_rows.h"
#include "cores/cd/cd_core.h"
#include "cores/core_profile.h"
#include "cores/psx/psx_disc_rules.h"
#include "cores/psx/psx_sector_program.h"
#include "cores/psx/psx_staging.h"
#include "svc/disc/cue_policy.h"
#include "svc/disc/track.h"

namespace mister::cores::manifests {

extern const CoreProfile kPsx;

inline constexpr std::size_t kMcdFrameBytes = 128;
inline constexpr std::size_t kMcdFrames = 64;

inline constexpr std::size_t kMcdImageBytes = kMcdFrameBytes * kMcdFrames;

inline constexpr std::size_t kMcdMagicFrame = 0;
inline constexpr std::size_t kMcdTestWriteFrame = 63;
inline constexpr std::size_t kMcdFirstFreeFrame = 1;
inline constexpr std::size_t kMcdLastFreeFrame = 15;
inline constexpr std::size_t kMcdFirstBrokenFrame = 16;
inline constexpr std::size_t kMcdLastBrokenFrame = 35;

inline constexpr std::size_t kMcdBlockPtrOffset = 8;
inline constexpr std::size_t kMcdBrokenListBytes = 4;

struct McdFrame {
    std::array<std::byte, kMcdFrameBytes> bytes{};

    constexpr void seal() noexcept {
        std::byte x{};
        for (std::size_t i = 0; i < kMcdFrameBytes - 1; ++i)
            x ^= bytes[i];
        bytes[kMcdFrameBytes - 1] = x;
    }
};

consteval std::array<std::byte, kMcdImageBytes> psx_mcd_header_image() {
    std::array<McdFrame, kMcdFrames> frames{};

    for (std::size_t f : {kMcdMagicFrame, kMcdTestWriteFrame}) {
        frames[f].bytes[0] = std::byte{'M'};
        frames[f].bytes[1] = std::byte{'C'};
    }

    for (std::size_t f = kMcdFirstFreeFrame; f <= kMcdLastFreeFrame; ++f) {
        frames[f].bytes[0] = std::byte{0xA0};
        frames[f].bytes[kMcdBlockPtrOffset] = std::byte{0xFF};
        frames[f].bytes[kMcdBlockPtrOffset + 1] = std::byte{0xFF};
    }

    for (std::size_t f = kMcdFirstBrokenFrame; f <= kMcdLastBrokenFrame; ++f) {
        for (std::size_t i = 0; i < kMcdBrokenListBytes; ++i)
            frames[f].bytes[i] = std::byte{0xFF};
        frames[f].bytes[kMcdBlockPtrOffset] = std::byte{0xFF};
        frames[f].bytes[kMcdBlockPtrOffset + 1] = std::byte{0xFF};
    }
    for (McdFrame& frame : frames)
        frame.seal();

    std::array<std::byte, kMcdImageBytes> img{};
    auto out = img.begin();
    for (const McdFrame& frame : frames)
        out = std::ranges::copy(frame.bytes, out).out;
    return img;
}

inline constexpr std::array<std::byte, kMcdImageBytes> kPsxMcdHeader = psx_mcd_header_image();

inline constexpr svc::BlankSaveSpec kPsxBlankSave{
    .fill = std::byte{0x00},
    .header = kPsxMcdHeader,
    .regions = {},
    .lba_bytes = proto::BlockSize{1024},
};

inline constexpr svc::disc::CuePolicy kPsxCuePolicy{
    .sub_naming = svc::disc::CuePolicy::SubNaming::None,
    .program = svc::disc::CuePolicy::Program::IndexTriple,
};

inline constexpr auto kPsxStartAssets = stock_start_chain("PSX");

inline constexpr auto kPsxBootAssets = std::to_array<BootAsset>({
    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageDir,
     .dest = WideIoIndex{0xC0},
     .group = BootGroup::Second,
     .exact_size = proto::FileSize{512 * 1024}},
    {.name = "cd_bios.rom",
     .where = {},
     .optional = true,
     .anchor = AssetAnchor::ImageParentDir,
     .dest = WideIoIndex{0xC0},
     .group = BootGroup::Second,
     .exact_size = proto::FileSize{512 * 1024}},
});

using psx::kPsxSlots;
using psx::kPsxStaging;

inline constexpr auto kPsxServices = std::to_array<LinkDecoderDecl>({
    {"psx.sector_kick",
     reactor::Cause::Tick,
     &kSectorKick<kPsx>,
     reactor::kEveryRound,
     RoundClaim::Never},
});

inline constexpr auto kPsxHostBits = std::to_array<HostBitRow>({
    {.fact = HostFact::SaveAutomountInhibit,
     .bits = {proto::StatusBit{63}, 1},
     .dir = HostBitRow::Dir::Read},
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
    .start = StartRule::Assets,
    .signatures = {},
    .blank_save = kPsxBlankSave,
    .staging = kPsxStaging,
    .tasty_playable = true,

    .block_drain_budget = 1,
    .block_sector_floor = proto::SectorFloor::at(proto::BlockSize{svc::disc::kRawFrameBytes}),
    .host_bits = kPsxHostBits,
    .no_stream_role = true,
    .host =
        {.suppress_stick_dpad = true,
         .image_rows_no_zip = true,

         .cheats = {.reset_on_remount = true}},
    .sector_program = infra::OptRef<const svc::disc::ISectorProgram>{psx::kPsxSectorProgram},
    .disc_rules = infra::OptRef<const svc::disc::IDiscRules>{psx::kPsxDiscRules},

    .grants = {Grant::SaveCopy, Grant::Discs},
};

std::unique_ptr<Core> make_psx(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<BootLadder> make_psx_ladder(const CoreProfile& p, const LadderContext& ctx);

}
