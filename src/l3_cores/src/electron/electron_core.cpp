// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/electron/electron_core.h"

#include "cores/core_adopt.h"
#include "infra/prelude.h"

namespace mister::cores::electron {

Ex<void> ElectronCore::do_adopt(const CoreAdopt&) {
    return {};
}

}
