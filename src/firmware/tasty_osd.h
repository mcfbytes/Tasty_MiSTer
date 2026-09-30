// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/osd_close.h"
#include "infra/seat.h"

namespace mister::fw {

class NullOsdClose final : public app::IOsdClose {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void close_osd() noexcept override { TASTY_SEAT_BODY(NullOsdClose); }
};

}  // namespace mister::fw
