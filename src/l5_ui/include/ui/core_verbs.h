// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "proto/bit_field.h"

namespace mister::ui {

struct CoreVerbs final {
    TASTY_SEAT_RESIDENT(Ui);
    std::optional<proto::BitField> save;
    std::optional<proto::BitField> restore;
};

}
