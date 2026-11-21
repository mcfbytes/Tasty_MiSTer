// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>

#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/megadrive/megadrive_windows.h"
#include "cores/pcm_census_rows.h"

namespace mister::cores::manifests {

extern const CoreProfile kMegaDrive;

inline constexpr auto kMegaDriveServices = std::to_array<LinkDecoderDecl>({
    {
        "megadrive.mdp_tick",
        reactor::Cause::Tick,
        &kPcmWireTick<kMegaDrive>,
        std::chrono::milliseconds{5},
        RoundClaim::Always,
    },
});

using megadrive::kMdPlusWindows;

inline constexpr CoreProfile kMegaDrive{
    .kind = CoreKind::MegaDrive,
    .name = "MegaDrive",
    .rbf = "MegaDrive",
    .services = kMegaDriveServices,
    .slots = {},
    .boot_assets = {},
    .signatures = {},
    .tasty_playable = true,

    .windows = kMdPlusWindows,
};

std::unique_ptr<Core> make_megadrive(const CoreProfile& p, const CoreGrant& g);

}
