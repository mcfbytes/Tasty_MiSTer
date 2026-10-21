// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/seat.h"
#include "ui/transition.h"
#include "ui/ui_event.h"

namespace mister::ui {

class MenuModel;

class HdKeyRemap final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Refresh {
        UiEvent ev;
        bool one_page = false;
    };

    struct Settled {
        Transition t;
        bool dropped = false;
    };

    [[nodiscard]] Refresh refresh(const MenuModel& model, const UiEvent& ev);
    [[nodiscard]] static Settled settle(const Transition& answer, std::size_t depth) noexcept;

    std::uint32_t steps_rewritten() const noexcept { return steps_; }

    std::uint32_t backs_rewritten() const noexcept { return backs_; }

private:
    [[nodiscard]] bool selected_row_steps(const MenuModel& model) const;

    std::uint32_t steps_ = 0;
    std::uint32_t backs_ = 0;
};

}
