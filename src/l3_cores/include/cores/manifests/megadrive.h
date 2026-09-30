// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/pcm_census_rows.h"

namespace mister::cores::manifests {

extern const CoreProfile kMegaDrive;

inline constexpr auto kMegaDriveServices = std::to_array<LinkDecoderDecl>({
    {
        "megadrive.mdp_tick",
        reactor::Cause::Tick,
        &kPcmWireTick<kMegaDrive>,
        5,
        DeadlineClass::A,
        OsdBudget::Pinned,
    },
});

inline constexpr auto kMdPlusWindows = std::to_array<CoreWindowDecl>({
    {.region = {0x1000'0000u, 0x0001'0000u, "mdplus-ring"}, .feeds_pcm = true},
});

inline constexpr CoreProfile kMegaDrive{
    .kind = CoreKind::MegaDrive,
    .name = "MegaDrive",
    .rbf = "MegaDrive",
    .services = kMegaDriveServices,
    .slots = {},
    .boot_assets = {},
    .signatures = {},

    .windows = kMdPlusWindows,
};

std::unique_ptr<Core> make_megadrive(const CoreProfile& p, const HostServices& h);

}  // namespace mister::cores::manifests
