// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/misc_page.h"

#include <array>
#include <string>
#include <string_view>

#include "ui/draw_classic.h"
#include "ui/osd_renderer.h"
#include "ui/page_ask.h"
#include "ui/page_context.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr char kStipple = '\x0b';
constexpr char kXorChar = '\x0c';

constexpr std::string_view kTitle = "Misc. Options";
constexpr std::string_view kConfigText = " Config:";
constexpr std::string_view kExitText = "            exit";

constexpr proto::OsdRow kConfigRow{10};
constexpr proto::OsdRow kExitRow{15};

constexpr RowIndex kConfigOrdinal{0};
constexpr RowIndex kExitOrdinal{1};
constexpr RowCount kMiscRows{2};

enum : std::size_t { kChoiceCount = svc::kMaxAltInis + 1 };

constexpr std::array<svc::IniChoice, kChoiceCount> kChoices{
    svc::IniChoice::Main,
    svc::IniChoice::Alt1,
    svc::IniChoice::Alt2,
    svc::IniChoice::Alt3
};

}

void MiscPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(MiscPage);
    asks_ = ctx.ui_requests;
    choice_ = ini_.sample().value.active;
    sel_ = kExitOrdinal;
}

PageFacts MiscPage::facts() const {
    TASTY_SEAT_BODY(MiscPage);
    PageFacts f = kDefaultPageFacts;
    f.osd_rows = kFullOsdRows;
    return f;
}

MenuMask MiscPage::mask() const {
    TASTY_SEAT_BODY(MiscPage);
    return dense_mask(kMiscRows);
}

bool MiscPage::present(svc::IniChoice c) const noexcept {
    TASTY_SEAT_BODY(MiscPage);
    return c == svc::IniChoice::Main || ini_.sample().value.has(c);
}

void MiscPage::cycle(bool forward) {
    TASTY_SEAT_BODY(MiscPage);
    const svc::IniCatalog& cat = ini_.sample().value;
    std::size_t at = 0;
    for (std::size_t i = 0; i < kChoiceCount; ++i) {
        if (kChoices[i] == choice_) {
            at = i;
            break;
        }
    }
    for (std::size_t step = 1; step < kChoiceCount; ++step) {
        const std::size_t next = forward ? (at + step) % kChoiceCount
                                         : (at + kChoiceCount - step) % kChoiceCount;
        const svc::IniChoice c = kChoices[next];

        if (c == svc::IniChoice::Main || cat.has(c)) {
            choice_ = c;
            return;
        }
    }
}

std::string MiscPage::row_text(proto::OsdRow row) const {
    TASTY_SEAT_BODY(MiscPage);
    if (row == kExitRow)
        return std::string{kExitText};
    if (row != kConfigRow)
        return {};

    const app::IniCell::Sample s = ini_.sample();
    const svc::IniCatalog& cat = s.value;
    const svc::IniChoice active = cat.active;

    std::string text{kConfigText};
    bool selected_before = false;
    for (const svc::IniChoice c : kChoices) {
        const bool en = present(c);
        const bool selected = en && c == active;
        if (selected)
            text.push_back(kXorChar);
        text.push_back(' ');
        if (selected_before) {
            text.push_back(kXorChar);
            text.push_back(' ');
        }
        selected_before = selected;
        if (!en)
            text.push_back(kStipple);
        text += cat.index.label_for(c);
        if (!en)
            text.push_back(kStipple);
    }
    text.push_back(' ');
    if (selected_before)
        text.push_back(kXorChar);
    return text;
}

bool MiscPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(MiscPage);
    (void)out.page.title.assign(kTitle);
    out.title_arrow = OsdRenderer::kArrowRight;
    out.count = 0;
    const RowCount height = osd_height();
    for (const RowIndex r : rows(height)) {
        const OsdRow at = osd_row_at(r.before());
        ClassicMarks marks{};
        marks.inverted = (at == kConfigRow && sel_ == kConfigOrdinal)
            || (at == kExitRow && sel_ == kExitOrdinal);
        marks.arrow = at == kExitRow ? OsdRenderer::kArrowRight : OsdRenderer::kArrowNone;
        out.rows[out.count++].classic = classic_row(at, row_text(at), marks);
    }
    return true;
}

Transition MiscPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(MiscPage);
    if (ev.buttons.has_any({UiEvent::Menu}))
        return Transition::leave();

    if (sel_ == kConfigOrdinal) {
        if (ev.buttons.has_any({UiEvent::Left | UiEvent::Minus})) {
            cycle(false);
            return Transition{};
        }
        if (ev.buttons.has_any({UiEvent::Right | UiEvent::Plus})) {
            cycle(true);
            return Transition{};
        }
        if (ev.buttons.has_any({UiEvent::Select}))
            switch_to_();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Select}))
        return Transition::leave();
    return Transition{};
}

void MiscPage::switch_to_() {
    TASTY_SEAT_BODY(MiscPage);
    (void)push_ask(asks_, app::UiRequest::SwitchIni{.ini = choice_});
}

}
