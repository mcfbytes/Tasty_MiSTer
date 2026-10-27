// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/seat.h"
#include "ui/navigator.h"
#include "ui/transition.h"
#include "ui/types.h"
#include "ui/ui_event.h"

namespace mister::ui {

class MenuModel;
class Page;

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

    struct Axis {
        enum class Move : std::uint8_t { None, Strip, Tier };
        Move move = Move::None;
        Navigator::Direction d = Navigator::Direction::Up;
        MenuMask run{};
    };

    [[nodiscard]] Axis axis(const MenuModel& model, std::uint16_t buttons);
    [[nodiscard]] static Axis axis_of(const Page& top, std::uint8_t across, std::uint16_t buttons);

    bool apply(const Axis& a, Navigator& nav) noexcept;

    void pin_across(std::uint8_t across) noexcept { pinned_across_ = across; }

    [[nodiscard]] Refresh refresh(const MenuModel& model, const UiEvent& ev);
    [[nodiscard]] static Settled settle(const Transition& answer, std::size_t depth) noexcept;

    std::uint32_t steps_rewritten() const noexcept { return steps_; }

    std::uint32_t backs_rewritten() const noexcept { return backs_; }

    std::uint32_t axis_moves() const noexcept { return axes_; }

private:
    [[nodiscard]] bool selected_row_steps(const MenuModel& model) const;

    std::uint32_t steps_ = 0;
    std::uint32_t backs_ = 0;
    std::uint32_t axes_ = 0;
    std::optional<std::uint8_t> pinned_across_;
};

}
