// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>

#include "cores/c64/easyflash_image.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core.h"
#include "cores/core_window_decl.h"
#include "cores/disk_mirror.h"
#include "cores/host_bit_row.h"
#include "cores/loader.h"
#include "cores/loader_context.h"
#include "proto/status_bit.h"
#include "svc/gcr_format_rows.h"

namespace mister::cores::manifests {

inline constexpr auto kC64HostBits = std::to_array<HostBitRow>({
    {.fact = HostFact::SaveUploadTrigger,
     .bits = {proto::StatusBit{61}, 1},
     .dir = HostBitRow::Dir::Pulse},
});

inline constexpr proto::FileSize kC64DriveHalf{0x0020'0000};

inline constexpr auto kC64Windows = std::to_array<CoreWindowDecl>({
    {.region = {0x1000'0000u, kC64DriveHalf.low_word(), "c64-drive8"}, .feeds_pcm = false},
    {.region = {0x1020'0000u, kC64DriveHalf.low_word(), "c64-drive9"}, .feeds_pcm = false},
});

inline constexpr std::array<FileSlot, 2> kC64Slots{{
    {.index = IoIndex{0},
     .extensions = "D64G64T64D81",
     .label = "Mount #8",
     .writable = true,
     .role = SlotRole::Save,
     .mirror =
         DiskMirror{
             .at = {.slot = CoreWindowSlot{0}, .offset = WindowOffset{0}},
             .room = kC64DriveHalf
         }},
    {.index = IoIndex{1},
     .extensions = "D64G64T64D81",
     .label = "Mount #9",
     .writable = true,
     .role = SlotRole::Save,
     .mirror = DiskMirror{
         .at = {.slot = CoreWindowSlot{1}, .offset = WindowOffset{0}},
         .room = kC64DriveHalf
     }},
}};

inline constexpr CoreProfile kC64{
    .kind = CoreKind::C64,
    .name = "C64",
    .rbf = "",
    .services = {},
    .slots = kC64Slots,
    .boot_assets = {},
    .signatures = {},
    .disk_formats = svc::kGcrFormatRows,
    .windows = kC64Windows,
    .host_bits = kC64HostBits,
    .undeclared_role = SlotRole::Save,
    .largest_save = c64::kEasyFlashImageBytes,
};

std::unique_ptr<ILoader> make_c64_loader(const LoaderContext& ctx);

std::unique_ptr<Core> make_c64(const CoreProfile& p, const CoreGrant& g);

}
