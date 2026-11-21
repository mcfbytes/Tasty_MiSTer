// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/autosave_knobs.h"
#include "app/save_director_status.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "svc/types.h"
#include "ui/indicator_board.h"

namespace mister::ui {

class AutosaveClock {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Inputs {

        app::AutosaveKnobs knobs{};

        std::optional<app::SaveDirectorStatus> status{};
    };

    bool round(const Inputs& in, IndicatorBoard& board) noexcept;

private:
    [[nodiscard]] bool post_(const Inputs& in, IndicatorBoard& board) noexcept;

    infra::Tally seen_saves_{};
    infra::Tally seen_faults_{};
};

}
