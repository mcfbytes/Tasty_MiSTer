// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/electron_core.h"

#include "cores/core_adopt.h"

namespace mister::cores {

Ex<void> ElectronCore::do_adopt(const CoreAdopt&) {
    return {};
}

}
