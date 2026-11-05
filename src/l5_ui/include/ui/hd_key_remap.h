// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/page_description.h"
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

    struct Across {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr bool any() const noexcept { return v != 0; }

        [[nodiscard]] constexpr bool draws(app::RowTier t) const noexcept {
            return (v & app::tier_bit(t)) != 0;
        }
    };

    struct Axis {
        enum class Move : std::uint8_t { None, Strip, Tier };
        Move move = Move::None;
        Navigator::Direction d = Navigator::Direction::Up;
        MenuMask run{};
    };

    [[nodiscard]] Axis axis(const MenuModel& model, UiButtonMask buttons);
    [[nodiscard]] Axis axis_of(const Page& top, Across across, UiButtonMask buttons);

    bool apply(const Axis& a, Navigator& nav) noexcept;

    void pin_across(Across across) noexcept { pinned_across_ = across; }

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
    std::optional<Across> pinned_across_;
};

}
