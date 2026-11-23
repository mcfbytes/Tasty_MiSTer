// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/core_profile.h"
#include "svc/gcr_format_rows.h"

namespace mister::cores::manifests {

inline constexpr std::array<FileSlot, 2> kC128Slots{{
    {.index = IoIndex{0},
     .extensions = "D64G64D71G71D81T64",
     .label = "Mount #8",
     .writable = true,
     .role = SlotRole::Save,
     .mount_index_form = MountIndexForm::ImageCaps},
    {.index = IoIndex{1},
     .extensions = "D64G64D71G71D81T64",
     .label = "Mount #9",
     .writable = true,
     .role = SlotRole::Save,
     .mount_index_form = MountIndexForm::ImageCaps},
}};

inline constexpr CoreProfile kC128{
    .kind = CoreKind::C128,
    .name = "C128",
    .rbf = "",
    .services = {},
    .slots = kC128Slots,
    .boot_assets = {},
    .signatures = {},
    .disk_formats = svc::kGcrFormatRows,
    .undeclared_start_mount = StartMount::Unmodelled,
};

}
