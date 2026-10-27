// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_key_remap.h"

#include <optional>

#include "app/hd_themes.h"
#include "app/page_description.h"
#include "ui/menu_model.h"
#include "ui/page.h"

namespace mister::ui {

bool HdKeyRemap::selected_row_steps(const MenuModel& model) const {
    const Page* top = model.stack().top();
    app::PageDescription d{};
    if (top == nullptr || !top->describe(d))
        return false;
    for (std::uint8_t i = 0; i < d.row_count && i < d.rows.size(); ++i) {
        if (d.rows[i].selected)
            return d.rows[i].steps;
    }
    return false;
}

HdKeyRemap::Axis HdKeyRemap::axis_of(const Page& top, std::uint8_t across, std::uint16_t buttons) {
    constexpr std::uint16_t kDirs = UiEvent::Up | UiEvent::Down | UiEvent::Left | UiEvent::Right;
    if (across == 0 || (buttons & kDirs) == 0)
        return Axis{};
    const std::optional<Page::TierRun> run = top.tier_run(top.selection());
    if (!run || (across & app::tier_bit(run->tier)) == 0)
        return Axis{};
    using D = Navigator::Direction;
    if ((buttons & UiEvent::Left) != 0)
        return Axis{Axis::Move::Strip, D::Up, run->rows};
    if ((buttons & UiEvent::Right) != 0)
        return Axis{Axis::Move::Strip, D::Down, run->rows};
    if ((buttons & UiEvent::Up) != 0)
        return Axis{Axis::Move::Tier, D::Up, run->rows};
    if ((buttons & UiEvent::Down) != 0)
        return Axis{Axis::Move::Tier, D::Down, run->rows};
    return Axis{};
}

HdKeyRemap::Axis HdKeyRemap::axis(const MenuModel& model, std::uint16_t buttons) {
    TASTY_SEAT_BODY(HdKeyRemap);
    const Page* top = model.stack().top();
    if (top == nullptr || !model.hd_tree())
        return Axis{};
    const std::uint8_t across = pinned_across_
        ? *pinned_across_
        : app::hd_horizontal_tiers(model.style().look().layout);
    return axis_of(*top, across, buttons);
}

bool HdKeyRemap::apply(const Axis& a, Navigator& nav) noexcept {
    bool moved = false;
    switch (a.move) {
        case Axis::Move::None:
            break;
        case Axis::Move::Strip:
            moved = nav.move_strip(a.d, a.run);
            break;
        case Axis::Move::Tier:
            moved = nav.move_tier(a.d, a.run);
            break;
    }
    if (moved)
        ++axes_;
    return moved;
}

HdKeyRemap::Refresh HdKeyRemap::refresh(const MenuModel& model, const UiEvent& ev) {
    TASTY_SEAT_BODY(HdKeyRemap);
    Refresh out{ev, false};
    if (!model.hd_tree())
        return out;

    const Page* top = model.stack().top();
    if ((ev.buttons & UiEvent::Back) != 0 && top != nullptr && top->back_is_idle())
        out.ev.buttons = static_cast<std::uint16_t>(
            (ev.buttons & ~UiEvent::Back) | UiEvent::Menu | UiEvent::PadBack
        );
    if ((out.ev.buttons & UiEvent::PadBack) != 0) {
        out.one_page = true;
        ++backs_;
        return out;
    }
    constexpr std::uint16_t kSides = UiEvent::Left | UiEvent::Right;
    if ((ev.buttons & kSides) == 0 || !selected_row_steps(model))
        return out;
    const std::uint16_t step = (ev.buttons & UiEvent::Right) != 0 ? UiEvent::Plus : UiEvent::Minus;
    out.ev.buttons = static_cast<std::uint16_t>((ev.buttons & ~kSides) | step);
    ++steps_;
    return out;
}

HdKeyRemap::Settled HdKeyRemap::settle(const Transition& answer, std::size_t depth) noexcept {
    const Transition back = depth > 1 ? Transition{Transition::Kind::Pop}
                                      : Transition{Transition::Kind::CloseAll};
    switch (answer.kind) {
        case Transition::Kind::Stay:
        case Transition::Kind::Pop:
            return Settled{answer, false};
        case Transition::Kind::CloseAll:
        case Transition::Kind::Leave:
            return Settled{back, false};
        case Transition::Kind::Push:
        case Transition::Kind::Replace:
            return Settled{back, true};
    }
    return Settled{back, false};
}

}
