// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/xthread/loan_channel.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/input/device_table.h"

namespace mister::app::input {

inline constexpr std::size_t kTableLeases = 2;
using DeviceTableLoans = xthread::
    LoanChannel<svc::input::DeviceTable, kTableLeases, SeatTag::Diag, SeatTag::Input>;

}
