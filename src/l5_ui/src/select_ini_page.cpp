// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/select_ini_page.h"

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

constexpr std::string_view kTitle = "Select INI";
constexpr std::string_view kHelp = "  Use dpad/cursor to select";
constexpr std::string_view kMainPad = "            ";
constexpr std::string_view kAlt1Pad = "      ";
constexpr std::string_view kAlt3Pad = "        ";
constexpr std::string_view kAlt2Pad = "            ";

constexpr proto::OsdRow kHelpRow{4};
constexpr proto::OsdRow kMainRow{7};
constexpr proto::OsdRow kAlt13Row{9};
constexpr proto::OsdRow kAlt2Row{11};

}

void SelectIniPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(SelectIniPage);
    asks_ = ctx.ui_requests;
}

bool SelectIniPage::offers(svc::IniChoice c) const noexcept {
    TASTY_SEAT_BODY(SelectIniPage);
    return ini_.sample().value.has(c);
}

PageFacts SelectIniPage::facts() const {
    TASTY_SEAT_BODY(SelectIniPage);
    PageFacts f = kDefaultPageFacts;
    f.osd_rows = kFullOsdRows;
    f.wake_every_round = true;
    return f;
}

std::string SelectIniPage::row_text(proto::OsdRow row) const {
    TASTY_SEAT_BODY(SelectIniPage);
    const app::IniCell::Sample s = ini_.sample();
    const svc::IniCatalog& cat = s.value;
    const auto label = [&cat](svc::IniChoice c) {
        return cat.index.label_for(c);
    };

    if (row == kHelpRow)
        return std::string{kHelp};
    if (row == kMainRow)
        return std::string{kMainPad} + label(svc::IniChoice::Main);
    if (row == kAlt2Row) {
        std::string text{kAlt2Pad};
        if (!cat.has(svc::IniChoice::Alt2))
            text.push_back(kStipple);
        text += label(svc::IniChoice::Alt2);
        if (!cat.has(svc::IniChoice::Alt2))
            text.push_back(kStipple);
        return text;
    }
    if (row == kAlt13Row) {
        std::string text{kAlt1Pad};
        if (!cat.has(svc::IniChoice::Alt1))
            text.push_back(kStipple);
        text += label(svc::IniChoice::Alt1);
        if (!cat.has(svc::IniChoice::Alt1))
            text.push_back(kStipple);
        text += kAlt3Pad;
        if (!cat.has(svc::IniChoice::Alt3))
            text.push_back(kStipple);
        text += label(svc::IniChoice::Alt3);
        if (!cat.has(svc::IniChoice::Alt3))
            text.push_back(kStipple);
        return text;
    }
    return {};
}

bool SelectIniPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(SelectIniPage);
    (void)out.page.title.assign(kTitle);
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;
    const RowCount height = osd_height();
    for (const RowIndex r : rows(height)) {
        const OsdRow at = osd_row_at(r.before());
        out.rows[out.count++].classic = classic_row(at, row_text(at), {});
    }
    return true;
}

Transition SelectIniPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(SelectIniPage);
    if (ev.buttons.has_any({UiEvent::Menu | UiEvent::Up}))
        return leave_();

    if (ev.buttons.has_any({UiEvent::Left})) {
        switch_to_(svc::IniChoice::Alt1);
    } else if (ev.buttons.has_any({UiEvent::Down})) {
        switch_to_(svc::IniChoice::Alt2);
    } else if (ev.buttons.has_any({UiEvent::Right})) {
        switch_to_(svc::IniChoice::Alt3);
    }
    return Transition{};
}

Transition SelectIniPage::leave_() {
    TASTY_SEAT_BODY(SelectIniPage);
    const app::IniCell::Sample s = ini_.sample();
    if (s && s.value.active != svc::IniChoice::Main) {
        (void)push_ask(asks_, app::UiRequest::SwitchIni{.ini = svc::IniChoice::Main});
    }
    return Transition::leave();
}

void SelectIniPage::switch_to_(svc::IniChoice c) {
    TASTY_SEAT_BODY(SelectIniPage);
    if (!offers(c))
        return;
    (void)push_ask(asks_, app::UiRequest::SwitchIni{.ini = c});
}

Transition SelectIniPage::on_tick(PageContext& ctx) {
    TASTY_SEAT_BODY(SelectIniPage);
    ctx.disk_activity.request();
    return Transition{};
}

}
