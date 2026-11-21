// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "app/launcher/uio_burst.h"
#include "infra/xthread/loan_channel.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::app::launcher {

inline constexpr std::size_t kScanoutSlots = 4;

using ScanoutChannel = xthread::
    LoanChannel<UioBurst, kScanoutSlots, SeatTag::Launcher, SeatTag::RT>;

}
