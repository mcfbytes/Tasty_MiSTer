// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/board_ops.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app {

class RtRelaxScope {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit RtRelaxScope(const BoardOps& ops) : rt_(ops.rt) {
        if (rt_)
            rt_->relax();
    }

    ~RtRelaxScope() {
        if (rt_)
            rt_->restore();
    }

    RtRelaxScope(const RtRelaxScope&) = delete;
    RtRelaxScope& operator=(const RtRelaxScope&) = delete;

private:
    infra::OptRef<IRtPriorityScope> rt_;
};

}
