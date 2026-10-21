// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/conf_str.h"

namespace mister::ui {

using proto::BitField;

struct StatusView {

    TASTY_SEAT_RESIDENT(Ui);

    std::uint16_t w[8] = {};

    std::uint32_t field(BitField b) const noexcept;
};

}
