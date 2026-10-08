// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "app/fb_view.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"

namespace mister::app {

struct HdFlipSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    std::uint32_t epoch = 0;
    std::uint32_t seq = 0;
    FbView view{};
};

static_assert(std::is_trivially_copyable_v<HdFlipSlot>);
static_assert(std::is_nothrow_default_constructible_v<HdFlipSlot>);

using HdFlipChannel = xthread::LoanChannel<HdFlipSlot, 2, SeatTag::HdOsd, SeatTag::Ui>;

}  // namespace mister::app
