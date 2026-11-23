// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/core_profile.h"
#include "svc/gcr_format_rows.h"

namespace mister::cores {

namespace detail {

inline constexpr std::array<LinkDecoderDecl, 0> kNoServices{};
inline constexpr std::array<FileSlot, 0> kNoSlots{};
inline constexpr std::array<BootAsset, 0> kNoAssets{};
inline constexpr std::array<SignatureRow, 0> kNoSignatures{};

}

inline constexpr CoreProfile kGenericProfile{
    .kind = CoreKind::Generic,
    .name = "",
    .rbf = "",
    .services = detail::kNoServices,
    .slots = detail::kNoSlots,
    .boot_assets = detail::kNoAssets,
    .signatures = detail::kNoSignatures,
    .disk_formats = svc::kGcrFormatRows,
    .undeclared_role = SlotRole::Save,
};

}
