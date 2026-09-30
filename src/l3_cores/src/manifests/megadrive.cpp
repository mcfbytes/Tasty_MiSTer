// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/megadrive.h"

#include "cores/megadrive_core.h"

namespace mister::cores::manifests {

static_assert(kMegaDriveServices[0].osd_budget == OsdBudget::Pinned,
              "the 5 ms PCM tick pins the OSD budget");

std::unique_ptr<Core> make_megadrive(const CoreProfile& p, const HostServices& h) {
    return std::make_unique<MegaDriveCore>(p, h);
}

}  // namespace mister::cores::manifests
