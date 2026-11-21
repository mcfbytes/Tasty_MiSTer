// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "app/hd/page_description.h"
#include "ui/confirm_launch.h"
#include "ui/confirm_page.h"
#include "ui/page_context.h"
#include "ui/hd_rows/hd_action.h"
#include "ui/hd_rows/hd_ask_row.h"
#include "ui/hd_rows/hd_close_row.h"
#include "ui/hd_rows/hd_gated_row.h"
#include "ui/hd_rows/hd_push_row.h"
#include "ui/hd_rows/hd_save_row.h"
#include "ui/hd_rows/hd_separator_row.h"
#include "ui/hd_rows/hd_value_row.h"
#include "ui/options_surface.h"
#include "ui/saved_baseline.h"

namespace mister::ui {
namespace {

using Row = app::hd::PageDescription::Row;
using RowKind = app::hd::PageDescription::RowKind;

constexpr std::array<std::string_view, 2> kYesNo{"Yes", "No"};

Row row_of(
    RowKind kind,
    std::string_view label,
    app::hd::RowTier tier = app::hd::RowTier::Place,
    app::hd::PageDescription::RowIcon icon = app::hd::PageDescription::RowIcon::None
) {
    Row r{};
    r.kind = kind;
    r.tier = tier;
    r.icon = icon;
    (void)r.label.assign(label);
    return r;
}

}

Row HdPushRow::describe(const RowCx&) const {
    TASTY_SEAT_BODY(HdPushRow);
    return row_of(RowKind::Submenu, spec_.label, spec_.tier, spec_.icon);
}

std::optional<RowKey> HdPushRow::key() const {
    TASTY_SEAT_BODY(HdPushRow);
    return push_row_key(spec_.page);
}

Transition HdPushRow::select(const RowCx&) {
    TASTY_SEAT_BODY(HdPushRow);
    return Transition::push(spec_.page, spec_.arg);
}

Row HdCloseRow::describe(const RowCx&) const {
    TASTY_SEAT_BODY(HdCloseRow);
    return row_of(RowKind::Action, spec_.label, spec_.tier);
}

Transition HdCloseRow::select(const RowCx&) {
    TASTY_SEAT_BODY(HdCloseRow);
    return Transition::close_all();
}

Row HdValueRow::describe(const RowCx&) const {
    TASTY_SEAT_BODY(HdValueRow);
    Row r = row_of(RowKind::Value, spec_.label);
    r.value = spec_.value;
    return r;
}

Transition HdValueRow::select(const RowCx&) {
    TASTY_SEAT_BODY(HdValueRow);
    return Transition{};
}

Row HdSeparatorRow::describe(const RowCx&) const {
    TASTY_SEAT_BODY(HdSeparatorRow);
    return row_of(RowKind::Separator, {});
}

Transition HdSeparatorRow::select(const RowCx&) {
    TASTY_SEAT_BODY(HdSeparatorRow);
    return Transition{};
}

bool HdAskRow::shown() const {
    TASTY_SEAT_BODY(HdAskRow);
    return spec_.confirm.has_value();
}

Row HdAskRow::describe(const RowCx&) const {
    TASTY_SEAT_BODY(HdAskRow);
    Row r = row_of(RowKind::Action, spec_.label, spec_.tier);
    r.value = spec_.value;
    return r;
}

Transition HdAskRow::select(const RowCx&) {
    TASTY_SEAT_BODY(HdAskRow);
    if (!spec_.confirm)
        return Transition{};
    ConfirmPage::Spec ask{};
    ask.title = spec_.label;
    ask.body = spec_.body;
    ask.choices = kYesNo;
    ask.host = infra::opt_ref_of(static_cast<IConfirmHost*>(this));
    ask.initial = spec_.initial;
    return spec_.confirm->open(PageId::Confirm, ask);
}

Transition HdAskRow::on_confirm(RowIndex choice) {
    TASTY_SEAT_BODY(HdAskRow);
    return choice == RowIndex{0} ? spec_.action.on_yes() : Transition::pop();
}

bool HdGatedRow::shown() const {
    TASTY_SEAT_BODY(HdGatedRow);
    return when_ && row_.shown();
}

std::optional<RowKey> HdGatedRow::key() const {
    TASTY_SEAT_BODY(HdGatedRow);
    return row_.key();
}

app::hd::PageDescription::Row HdGatedRow::describe(const RowCx& cx) const {
    TASTY_SEAT_BODY(HdGatedRow);
    return row_.describe(cx);
}

Transition HdGatedRow::select(const RowCx& cx) {
    TASTY_SEAT_BODY(HdGatedRow);
    return row_.select(cx);
}

Transition HdGatedRow::step(CycleDir dir, const RowCx& cx) {
    TASTY_SEAT_BODY(HdGatedRow);
    return row_.step(dir, cx);
}

std::optional<RowKey> HdSaveRow::key() const {
    TASTY_SEAT_BODY(HdSaveRow);
    return RowKey::save();
}

Row HdSaveRow::describe(const RowCx&) const {
    TASTY_SEAT_BODY(HdSaveRow);
    Row r{};
    r.kind = RowKind::Action;
    (void)r.label.assign("Save settings");
    if (baseline_.unsaved(host_.live_words()))
        (void)r.value.assign("Unsaved");
    return r;
}

Transition HdSaveRow::select(const RowCx&) {
    TASTY_SEAT_BODY(HdSaveRow);
    host_.save_config();
    return Transition{};
}

}
