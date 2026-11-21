// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "infra/tally.h"
#include "svc/types.h"

namespace mister::ui {

class ConnectRefusals {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void note_full() noexcept { full_.add(); }

    void note_unwired() noexcept { unwired_.add(); }

    [[nodiscard]] infra::Tally full() const noexcept { return full_; }

    [[nodiscard]] infra::Tally unwired() const noexcept { return unwired_; }

    [[nodiscard]] infra::Tally total() const noexcept { return full_.sum(unwired_); }

private:
    infra::Tally full_{};
    infra::Tally unwired_{};
};

}
