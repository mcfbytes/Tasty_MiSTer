// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "svc/connect/view_gen.h"

namespace mister::app::connect {

class ViewMark {
    TASTY_SEAT_RESIDENT(Connect);

public:
    [[nodiscard]] bool take(svc::connect::ViewGen gen) noexcept {
        if (seen_ && *seen_ == gen)
            return false;
        seen_ = gen;
        return true;
    }

private:
    std::optional<svc::connect::ViewGen> seen_{};
};

}
