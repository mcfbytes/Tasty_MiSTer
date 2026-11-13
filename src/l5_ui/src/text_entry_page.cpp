// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/text_entry_page.h"

#include <array>
#include <cstring>
#include <utility>
#include <linux/input-event-codes.h>

#include "app/hd_window.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "os/passkey.h"
#include "os/passphrase.h"
#include "os/pin_code.h"
#include "os/ssid.h"
#include "svc/types.h"
#include "ui/draw_classic.h"
#include "ui/keyboard_text.h"
#include "ui/osd_renderer.h"
#include "ui/page_walk.h"
#include "ui/text_entry_host.h"
#include "ui/ui_event.h"

namespace mister::ui {
namespace {

using Row = app::PageDescription::Row;
using RowKind = app::PageDescription::RowKind;

constexpr char kMask = '*';
constexpr std::size_t kFieldChars = 22;

Row field_row(std::string_view heading, std::string_view text, bool masked) {
    Row r{};
    r.kind = RowKind::Text;
    std::array<char, EntryBuffer::kCapacity + 1> shown{};
    std::size_t n = 0;
    const std::size_t from = text.size() > kFieldChars ? text.size() - kFieldChars : 0;
    for (std::size_t i = from; i < text.size(); ++i)
        shown[n++] = masked ? kMask : text[i];
    shown[n++] = '_';
    (void)r.label.assign(heading);
    (void)r.value.assign(std::string_view{shown.data(), n});
    return r;
}

void scrub_result(TextEntryResult& result) noexcept {
    explicit_bzero(&result, sizeof(result));
}

}

TextEntryPage::TextEntryPage(const TextEntryRequest& request) noexcept
    : request_(request),
      grid_(request.kind) {
    TASTY_SEAT_BODY(TextEntryPage);
    sel_ = RowIndex::of(infra::ordinal(Line::Grid0));
}

PageId TextEntryPage::id() const {
    TASTY_SEAT_BODY(TextEntryPage);
    return PageId::TextEntry;
}

svc::Tally TextEntryPage::refusals() const noexcept {
    TASTY_SEAT_BODY(TextEntryPage);
    return refusals_;
}

bool TextEntryPage::valid_() const noexcept {
    switch (request_.kind) {
        case TextEntryKind::Passphrase:
            return os::Passphrase::parse(text_.view()).has_value();
        case TextEntryKind::HiddenSsid:
            return !text_.view().empty() && os::Ssid::parse(text_.view()).has_value();
        case TextEntryKind::Passkey:
            return os::Passkey::parse(text_.view()).has_value();
        case TextEntryKind::Pin:
            return os::PinCode::parse(text_.view()).has_value();
    }
    return false;
}

bool TextEntryPage::accepts_(char c) const noexcept {
    if (!text_entry_numeric(request_.kind))
        return true;
    return os::Passkey::is_digit(c);
}

bool TextEntryPage::shown_(Line line) const noexcept {
    const bool numeric = text_entry_numeric(request_.kind);
    switch (line) {
        case Line::Layer:
        case Line::Space:
            return !numeric;
        case Line::Reveal:
            return text_entry_masked(request_.kind);
        case Line::Done:
            return valid_();
        case Line::Field:
        case Line::Grid0:
        case Line::Grid1:
        case Line::Grid2:
        case Line::Grid3:
        case Line::Backspace:
        case Line::Cancel:
        case Line::kCount:
            return true;
    }
    return false;
}

bool TextEntryPage::focusable_(Line line) const noexcept {
    return line != Line::Field && line != Line::kCount && shown_(line);
}

std::optional<RowIndex> TextEntryPage::grid_row_(Line line) noexcept {
    const std::size_t at = infra::ordinal(line);
    const std::size_t first = infra::ordinal(Line::Grid0);
    if (at < first || at >= first + KeyGrid::kRows)
        return std::nullopt;
    return RowIndex::of(at - first);
}

TextEntryPage::Line TextEntryPage::line_at_(RowIndex i) const noexcept {
    const infra::OptRef<const Line> at = RowSpan<const Line>{kLines}.find(i);
    return at ? *at : Line::kCount;
}

MenuMask TextEntryPage::mask() const {
    TASTY_SEAT_BODY(TextEntryPage);
    MenuMask m{};
    for (std::size_t i = 0; i < kLines.size(); ++i) {
        if (focusable_(kLines[i]))
            m = m.with(RowIndex::of(i));
    }
    return m;
}

void TextEntryPage::set_selection(RowIndex i) {
    TASTY_SEAT_BODY(TextEntryPage);
    sel_ = i;
    typing_ = false;
}

RowIndex TextEntryPage::selection() const {
    TASTY_SEAT_BODY(TextEntryPage);
    return sel_;
}

bool TextEntryPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(TextEntryPage);
    (void)out.page.title.assign(
        request_.title.empty() ? text_entry_heading(request_.kind) : request_.title
    );
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const bool masked = text_entry_masked(request_.kind) && !revealed_;
    std::size_t total = 0;
    std::size_t selected_at = 0;
    std::array<Line, kLines.size()> order{};
    for (std::size_t i = 0; i < kLines.size(); ++i) {
        const Line line = kLines[i];
        if (!shown_(line))
            continue;
        if (RowIndex::of(i) == sel_)
            selected_at = total;
        order[total++] = line;
    }

    const app::HdWindow win = out.window(RowIndex::of(selected_at), RowCount::of(total));
    for (std::size_t k = 0; k < total; ++k) {
        const Line line = order[k];
        Row hd{};
        std::string text;
        const bool selected = RowIndex::of(infra::ordinal(line)) == sel_ && line != Line::Field;
        if (line == Line::Field) {
            hd = field_row(text_entry_heading(request_.kind), text_.view(), masked);
        } else if (const std::optional<RowIndex> g = grid_row_(line)) {
            hd.kind = RowKind::Value;
            hd.steps = true;
            hd.value = grid_.line(*g, selected);
            (void)hd.label.assign("Keys");
        } else if (line == Line::Layer) {
            hd.kind = RowKind::Value;
            (void)hd.label.assign("Keyboard");
            (void)hd.value.assign(layer_label(grid_.layer()));
        } else if (line == Line::Reveal) {
            hd.kind = RowKind::Value;
            (void)hd.label.assign("Show text");
            (void)hd.value.assign(revealed_ ? "On" : "Off");
        } else if (line == Line::Space) {
            hd.kind = RowKind::Action;
            (void)hd.label.assign("Space");
        } else if (line == Line::Backspace) {
            hd.kind = RowKind::Action;
            (void)hd.label.assign("Backspace");
        } else if (line == Line::Done) {
            hd.kind = RowKind::Action;
            (void)hd.label.assign("Done");
        } else {
            hd.kind = RowKind::Action;
            (void)hd.label.assign("Cancel");
        }
        hd.selected = selected;
        text.assign(hd.label.view());
        if (!hd.value.empty())
            text.append("  ").append(hd.value.view());

        const bool in_window = RowRange{win.first, win.count}.contains(RowIndex::of(k));
        if (in_window) {
            PageRow& r = out.rows[out.count++];
            r.has_hd = true;
            r.hd = hd;
            r.classic = classic_row(
                osd_row_at(RowCount::of(k)),
                text,
                {.inverted = selected, .arrow = OsdRenderer::kArrowNone}
            );
        } else if (std::cmp_less(out.count, out.rows.size())) {
            out.rows[out.count++].classic = classic_row(
                osd_row_at(RowCount::of(k)),
                text,
                {.inverted = selected, .arrow = OsdRenderer::kArrowNone}
            );
        }
    }
    return true;
}

Transition TextEntryPage::cancel_() {
    text_.scrub();
    if (!request_.host)
        return Transition::pop();
    return request_.host->on_cancel();
}

Transition TextEntryPage::finish_() {
    if (!valid_()) {
        refusals_.add();
        return Transition{};
    }
    TextEntryResult result{};
    switch (request_.kind) {
        case TextEntryKind::Passphrase:
            result = infra::make<TextEntryResult>(
                TextEntryResult::Passphrase{*os::Passphrase::parse(text_.view())}
            );
            break;
        case TextEntryKind::HiddenSsid:
            result = infra::make<TextEntryResult>(
                TextEntryResult::Ssid{*os::Ssid::parse(text_.view())}
            );
            break;
        case TextEntryKind::Passkey:
            result = infra::make<TextEntryResult>(
                TextEntryResult::Passkey{*os::Passkey::parse(text_.view())}
            );
            break;
        case TextEntryKind::Pin:
            result = infra::make<TextEntryResult>(
                TextEntryResult::Pin{*os::PinCode::parse(text_.view())}
            );
            break;
    }
    text_.scrub();
    Transition next = Transition::pop();
    if (request_.host)
        next = request_.host->on_entered(result);
    scrub_result(result);
    return next;
}

Transition TextEntryPage::act_(Line line) {
    if (const std::optional<RowIndex> g = grid_row_(line)) {
        const std::optional<char> key = grid_.key_at(*g);
        if (key && !text_.push(*key))
            refusals_.add();
        return Transition{};
    }
    switch (line) {
        case Line::Layer:
            grid_.toggle_layer();
            return Transition{};
        case Line::Reveal:
            revealed_ = !revealed_;
            return Transition{};
        case Line::Space:
            if (!text_.push(' '))
                refusals_.add();
            return Transition{};
        case Line::Backspace:
            text_.pop();
            return Transition{};
        case Line::Done:
            return finish_();
        case Line::Cancel:
            return cancel_();
        case Line::Field:
        case Line::Grid0:
        case Line::Grid1:
        case Line::Grid2:
        case Line::Grid3:
        case Line::kCount:
            return Transition{};
    }
    return Transition{};
}

Transition TextEntryPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(TextEntryPage);
    shift_ = KeyboardText::shift_after(ev.raw, shift_);
    const svc::EvdevCode key = svc::EvdevCode::of(ev.raw);
    const bool make = !ev.raw.is_release();

    if (make) {
        if (const std::optional<char> c = KeyboardText::of(ev.raw, shift_)) {
            if (accepts_(*c)) {
                typing_ = true;
                if (!text_.push(*c))
                    refusals_.add();
                return Transition{};
            }
        }
    }

    if (ev.buttons.has_any({UiEvent::Menu}))
        return cancel_();
    if (ev.buttons.has_any({UiEvent::Back})) {
        text_.pop();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Select})) {
        const bool enter = key == svc::EvdevCode{KEY_ENTER} || key == svc::EvdevCode{KEY_KPENTER};
        if (typing_ && enter && valid_())
            return finish_();
        return act_(line_at_(sel_));
    }

    const std::optional<RowIndex> g = grid_row_(line_at_(sel_));
    const bool left = ev.buttons.has_any({UiEvent::Left | UiEvent::Minus});
    const bool right = ev.buttons.has_any({UiEvent::Right | UiEvent::Plus});
    if (g && (left || right)) {
        typing_ = false;
        grid_.step(*g, right ? CycleDir::Next : CycleDir::Back);
    }
    return Transition{};
}

}
