// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "svc/types.h"

namespace mister::ui {

class ConnectRefusals {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void note_full() noexcept { full_.add(); }

    void note_unwired() noexcept { unwired_.add(); }

    [[nodiscard]] svc::Tally full() const noexcept { return full_; }

    [[nodiscard]] svc::Tally unwired() const noexcept { return unwired_; }

    [[nodiscard]] svc::Tally total() const noexcept { return full_.sum(unwired_); }

private:
    svc::Tally full_{};
    svc::Tally unwired_{};
};

}
