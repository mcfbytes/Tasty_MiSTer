// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"
#include "ui/bt_ops.h"

namespace mister::ui {

class ShellBtOps final : public IBtOps {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] Ex<bool> adapter_up() override;
    [[nodiscard]] Ex<void> reset_adapter() override;
    [[nodiscard]] Ex<void> renew_pairings() override;
};

}
