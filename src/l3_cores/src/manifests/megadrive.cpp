// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/megadrive.h"

#include "cores/megadrive_core.h"

namespace mister::cores::manifests {

static_assert(
    kMegaDriveServices[0].round_claim == RoundClaim::Always,
    "the 5 ms PCM tick claims each round it runs (no OSD row)"
);

std::unique_ptr<Core> make_megadrive(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<MegaDriveCore>(p, g.services);
}

}
