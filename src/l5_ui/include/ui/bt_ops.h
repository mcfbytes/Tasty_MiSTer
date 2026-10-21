// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"

namespace mister::ui {

class IBtOps {
public:
    virtual ~IBtOps() = default;

    [[nodiscard]] virtual Ex<bool> adapter_up() = 0;
    [[nodiscard]] virtual Ex<void> reset_adapter() = 0;

    [[nodiscard]] virtual Ex<void> renew_pairings() = 0;

protected:
    IBtOps() = default;
    IBtOps(const IBtOps&) = default;
    IBtOps& operator=(const IBtOps&) = default;
};

}
