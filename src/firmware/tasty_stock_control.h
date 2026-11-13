// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"
#include "os/process_name.h"

namespace mister::fw {

class StockControl {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit StockControl(os::ProcessName stock) noexcept : stock_(stock) {}

    [[nodiscard]] Ex<bool> stop() const noexcept;

private:
    os::ProcessName stock_;
};

}
