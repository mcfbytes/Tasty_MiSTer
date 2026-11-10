// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/core_profile.h"

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
    .undeclared_role = SlotRole::Save,
};

inline constexpr CoreProfile kMenuProfile{
    .kind = CoreKind::Menu,
    .name = "MENU",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .start = StartRule::None,
    .signatures = {},
    .is_front_end = true,
    .tasty_playable = true,
    .undeclared_start_mount = StartMount::Unmodelled,
};

}
