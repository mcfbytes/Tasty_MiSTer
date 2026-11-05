// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/conf_str.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::ui {

using proto::BitField;

struct StatusView {

    TASTY_SEAT_RESIDENT(Ui);

    proto::StatusWord status{};

    proto::FieldValue field(BitField b) const noexcept;
};

}
