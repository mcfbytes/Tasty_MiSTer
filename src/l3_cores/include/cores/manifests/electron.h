// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/loader.h"
#include "cores/loader_context.h"

namespace mister::cores::manifests {

inline constexpr CoreProfile kElectron{
    .kind = CoreKind::Electron,
    .name = "AcornElectron",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .signatures = {},
};

std::unique_ptr<ILoader> make_uef_loader(const LoaderContext& ctx);

std::unique_ptr<Core> make_electron(const CoreProfile& p, const CoreGrant& g);

}
