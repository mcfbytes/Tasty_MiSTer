// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"

namespace mister::cores::manifests {

inline constexpr CoreProfile kElectron{
    .kind = CoreKind::Electron,
    .name = "AcornElectron",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .signatures = {},
    .file_tx_whole = true,
};

std::unique_ptr<Core> make_electron(const CoreProfile& p, const CoreGrant& g);

}
