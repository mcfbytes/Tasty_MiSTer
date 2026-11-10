// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/loan_channel.h"
#include "infra/seat.h"
#include "svc/device_table.h"

namespace mister::app {

inline constexpr std::size_t kTableLeases = 2;
using DeviceTableLoans = xthread::
    LoanChannel<svc::DeviceTable, kTableLeases, SeatTag::Diag, SeatTag::Input>;

}
