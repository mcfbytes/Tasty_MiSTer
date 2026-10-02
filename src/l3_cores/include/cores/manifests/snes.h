// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/core_profile.h"
#include "cores/core_support.h"

namespace mister::cores::manifests {

inline constexpr auto kSnesStartAssets = stock_start_chain("SNES");

inline constexpr CoreProfile kSnes{
    .kind = CoreKind::Snes,
    .name = "SNES",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .start_assets = kSnesStartAssets,
    .signatures = {},
    .file_tx_whole = true,
};

std::unique_ptr<Core> make_snes(const CoreProfile& p, const HostServices& h);

}  // namespace mister::cores::manifests
