// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/types.h"

#include "hal/ocram_types.h"

namespace mister::proto {

OsdRowCount OsdRowCount::from(hal::ocram::ImageRows rows) noexcept {
    return saturating(rows.size());
}

}
