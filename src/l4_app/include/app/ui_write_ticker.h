// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/recents/ui_write_lane.h"
#include "app/recents/verdict_sinks.h"
#include "infra/xthread/coworker.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::app {

class UiWriteTicker final : public xthread::ICoworker {
    TASTY_SEAT_RESIDENT(Ui);

public:
    UiWriteTicker(app::recents::UiWriteLane& lane, app::recents::VerdictSinks sinks) noexcept
        : lane_(lane),
          sinks_(sinks) {}

    void serve() noexcept override {
        TASTY_SEAT_BODY(UiWriteTicker);
        lane_.tick(sinks_);
    }

    void on_stop() noexcept override {
        TASTY_SEAT_BODY(UiWriteTicker);
        lane_.tick(sinks_);
    }

private:
    app::recents::UiWriteLane& lane_;
    const app::recents::VerdictSinks sinks_;
};

}
