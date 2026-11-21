// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/save_state_attach.h"
#include "infra/xthread/coworker.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::app {

class SaveStateUiTicker final : public xthread::ICoworker {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit SaveStateUiTicker(SaveStateAttach& attach) noexcept : attach_(attach) {}

    void serve() noexcept override {
        TASTY_SEAT_BODY(SaveStateUiTicker);
        attach_.tick();
    }

    void on_pause() noexcept override {
        TASTY_SEAT_BODY(SaveStateUiTicker);
        attach_.on_ui_pause();
    }

private:
    SaveStateAttach& attach_;
};

}
