// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_key_remap.h"

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

HdKeyRemap::Refresh HdKeyRemap::refresh(const MenuModel& model, const UiEvent& ev) {
    TASTY_SEAT_BODY(HdKeyRemap);
    Refresh out{ev, false};
    if (!model.hd_tree())
        return out;
    if ((ev.buttons & UiEvent::PadBack) != 0) {
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
