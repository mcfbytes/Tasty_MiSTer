// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/core_profile.h"
#include "cores/sidecar_builder.h"

namespace mister::cores::manifests {

inline constexpr CoreProfile kZx81{
    .kind = CoreKind::Zx81,
    .name = "ZX81",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .signatures = {},
    .undeclared_role = SlotRole::Save,
};

std::unique_ptr<ISidecarBuilder> make_zx81_sidecars();

}
