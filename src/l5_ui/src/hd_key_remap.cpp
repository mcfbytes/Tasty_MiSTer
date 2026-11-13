// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_key_remap.h"

#include <array>
#include <optional>

#include "app/hd_themes.h"
#include "app/page_description.h"
#include "infra/message_sum.h"
#include "ui/menu_model.h"
#include "ui/page.h"

namespace mister::ui {

bool HdKeyRemap::selected_row_steps(const MenuModel& model) const {
    const auto top = model.stack().top();
    app::PageDescription d{};
    if (!top || !top->describe(d))
        return false;
    for (const RowIndex r : rows(d.listed())) {
        const app::PageDescription::Row& row = d.row(r);
        if (row.selected)
            return row.steps;
    }
    return false;
}

HdKeyRemap::Axis HdKeyRemap::axis_of(const Page& top, app::TierSet across, UiButtonMask buttons) {
    TASTY_SEAT_BODY(HdKeyRemap);
    constexpr std::uint16_t kDirs = UiEvent::Up | UiEvent::Down | UiEvent::Left | UiEvent::Right;
    if (!across.any() || !buttons.has_any({kDirs}))
        return Axis{};
    const std::optional<Page::TierRun> run = top.tier_run(top.selection());
    if (!run || !across.has(run->tier))
        return Axis{};
    using D = Navigator::Direction;
    if (buttons.has_any({UiEvent::Left}))
        return Axis{Axis::Move::Strip, D::Up, run->rows};
    if (buttons.has_any({UiEvent::Right}))
        return Axis{Axis::Move::Strip, D::Down, run->rows};
    if (buttons.has_any({UiEvent::Up}))
        return Axis{Axis::Move::Tier, D::Up, run->rows};
    if (buttons.has_any({UiEvent::Down}))
        return Axis{Axis::Move::Tier, D::Down, run->rows};
    return Axis{};
}

HdKeyRemap::Axis HdKeyRemap::axis(const MenuModel& model, UiButtonMask buttons) {
    TASTY_SEAT_BODY(HdKeyRemap);
    const auto top = model.stack().top();
    if (!top || !model.hd_tree())
        return Axis{};
    const app::TierSet across = pinned_across_
        ? *pinned_across_
        : app::hd_horizontal_tiers(model.style().look().layout);
    return axis_of(*top, across, buttons);
}

bool HdKeyRemap::apply(const Axis& a, Navigator& nav) noexcept {
    TASTY_SEAT_BODY(HdKeyRemap);
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

    const auto top = model.stack().top();
    if (ev.buttons.has_any({UiEvent::Back}) && top && top->back_is_idle())
        out.ev.buttons = ev.buttons.without({UiEvent::Back})
                             .with({UiEvent::Menu | UiEvent::PadBack});
    if (out.ev.buttons.has_any({UiEvent::PadBack})) {
        out.one_page = true;
        ++backs_;
        return out;
    }
    constexpr std::uint16_t kSides = UiEvent::Left | UiEvent::Right;
    if (!ev.buttons.has_any({kSides}) || !selected_row_steps(model))
        return out;
    const UiButtonMask step{ev.buttons.has_any({UiEvent::Right}) ? UiEvent::Plus : UiEvent::Minus};
    out.ev.buttons = ev.buttons.without({kSides}).with(step);
    ++steps_;
    return out;
}

namespace {

enum class PadBack : std::uint8_t { Keep, Back, BackDropped };

struct PadBackRow {
    Transition::Kind kind;
    PadBack settles;
};

constexpr auto kPadBack = std::to_array<PadBackRow>({
    {Transition::Kind::Stay, PadBack::Keep},
    {Transition::Kind::Push, PadBack::BackDropped},
    {Transition::Kind::Pop, PadBack::Keep},
    {Transition::Kind::Replace, PadBack::BackDropped},
    {Transition::Kind::CloseAll, PadBack::Back},
    {Transition::Kind::Leave, PadBack::Back},
    {Transition::Kind::ChooseHdStyle, PadBack::BackDropped},
    {Transition::Kind::ChooseClassicStyle, PadBack::Back},
});
static_assert(infra::rows_are_ordinal(kPadBack), "every Transition kind names its pad-B answer");

}

HdKeyRemap::Settled HdKeyRemap::settle(const Transition& answer, std::size_t depth) noexcept {
    TASTY_SEAT_BODY(HdKeyRemap);
    const Transition back = depth > 1 ? Transition::pop() : Transition::close_all();
    switch (kPadBack[infra::ordinal(answer.kind())].settles) {
        case PadBack::Keep:
            return Settled{answer, false};
        case PadBack::Back:
            return Settled{back, false};
        case PadBack::BackDropped:
            return Settled{back, true};
    }
    return Settled{back, false};
}

}
