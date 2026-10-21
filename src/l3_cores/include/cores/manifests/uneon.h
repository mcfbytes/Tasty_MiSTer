// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"

namespace mister::cores::manifests {

inline constexpr CoreProfile kUneon{
    .kind = CoreKind::Uneon,
    .name = "Uneon",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .signatures = {},
    .block_drain_budget = 3,
    .undeclared_start_mount = StartMount::Unmodelled,
};

}
