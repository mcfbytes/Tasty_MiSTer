// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

class ActKeys final {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] proto::ActKey next() noexcept {
        gen_ = gen_.next();
        return gen_;
    }

private:
    proto::ActKey gen_{};
};

}
