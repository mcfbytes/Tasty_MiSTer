// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "app/page_description.h"
#include "ui/confirm_launch.h"
#include "ui/confirm_page.h"
#include "ui/hd_label.h"
#include "ui/page_context.h"
#include "ui/script_launch.h"
#include "ui/hd_rows/hd_action.h"
#include "ui/hd_rows/hd_ask_row.h"
#include "ui/hd_rows/hd_close_row.h"
#include "ui/hd_rows/hd_gated_row.h"
#include "ui/hd_rows/hd_option_row.h"
#include "ui/hd_rows/hd_pair_row.h"
#include "ui/hd_rows/hd_push_row.h"
#include "ui/hd_rows/hd_save_row.h"
#include "ui/hd_rows/hd_separator_row.h"
#include "ui/hd_rows/hd_value_row.h"
#include "ui/options_model.h"
#include "ui/option_item.h"
#include "ui/options_surface.h"
#include "ui/saved_baseline.h"

namespace mister::ui {
namespace {

using Row = app::PageDescription::Row;
using RowKind = app::PageDescription::RowKind;

constexpr std::array<std::string_view, 2> kYesNo{"Yes", "No"};

Row row_of(
    RowKind kind,
    std::string_view label,
    app::RowTier tier = app::RowTier::Place,
    app::PageDescription::RowIcon icon = app::PageDescription::RowIcon::None
) {
    Row r{};
    r.kind = kind;
    r.tier = tier;
    r.icon = icon;
    (void)r.label.assign(label);
    return r;
}

}

Row HdPushRow::describe() const {
    TASTY_SEAT_BODY(HdPushRow);
    return row_of(RowKind::Submenu, spec_.label, spec_.tier, spec_.icon);
}

std::optional<RowKey> HdPushRow::key() const {
    TASTY_SEAT_BODY(HdPushRow);
    return RowKey::in_space(RowKeySpace::Push, item_pushing(spec_.page, spec_.focus));
}

Transition HdPushRow::select() {
    TASTY_SEAT_BODY(HdPushRow);
    return Transition{Transition::Kind::Push, spec_.page, spec_.focus};
}

Row HdCloseRow::describe() const {
    TASTY_SEAT_BODY(HdCloseRow);
    return row_of(RowKind::Action, spec_.label, spec_.tier);
}

Transition HdCloseRow::select() {
    TASTY_SEAT_BODY(HdCloseRow);
    return Transition{Transition::Kind::CloseAll};
}

Row HdValueRow::describe() const {
    TASTY_SEAT_BODY(HdValueRow);
    Row r = row_of(RowKind::Value, spec_.label);
    r.value = spec_.value;
    return r;
}

Transition HdValueRow::select() {
    TASTY_SEAT_BODY(HdValueRow);
    return Transition{};
}

Row HdSeparatorRow::describe() const {
    TASTY_SEAT_BODY(HdSeparatorRow);
    return row_of(RowKind::Separator, {});
}

Transition HdSeparatorRow::select() {
    TASTY_SEAT_BODY(HdSeparatorRow);
    return Transition{};
}

bool HdAskRow::shown() const {
    TASTY_SEAT_BODY(HdAskRow);
    return spec_.confirm.has_value();
}

Row HdAskRow::describe() const {
    TASTY_SEAT_BODY(HdAskRow);
    Row r = row_of(RowKind::Action, spec_.label, spec_.tier);
    r.value = spec_.value;
    return r;
}

Transition HdAskRow::select() {
    TASTY_SEAT_BODY(HdAskRow);
    if (!spec_.confirm)
        return Transition{};
    ConfirmPage::Spec ask{};
    ask.title = spec_.label;
    ask.body = spec_.body;
    ask.choices = kYesNo;
    ask.host = infra::opt_ref_of(static_cast<IConfirmHost*>(this));
    ask.initial = spec_.initial;
    spec_.confirm->pending = ask;
    return Transition{Transition::Kind::Push, PageId::Confirm};
}

Transition HdAskRow::on_confirm(RowIndex choice) {
    TASTY_SEAT_BODY(HdAskRow);
    return choice == RowIndex{0} ? spec_.action.on_yes() : Transition{Transition::Kind::Pop};
}

bool HdGatedRow::shown() const {
    TASTY_SEAT_BODY(HdGatedRow);
    return when_ && row_.shown();
}

std::optional<RowKey> HdGatedRow::key() const {
    TASTY_SEAT_BODY(HdGatedRow);
    return row_.key();
}

Row HdGatedRow::describe() const {
    TASTY_SEAT_BODY(HdGatedRow);
    return row_.describe();
}

Transition HdGatedRow::select() {
    TASTY_SEAT_BODY(HdGatedRow);
    return row_.select();
}

Transition HdGatedRow::step(CycleDir dir) {
    TASTY_SEAT_BODY(HdGatedRow);
    return row_.step(dir);
}

bool HdPairRow::shown() const {
    TASTY_SEAT_BODY(HdPairRow);
    return ctx_.launches.script.has_value();
}

Row HdPairRow::describe() const {
    TASTY_SEAT_BODY(HdPairRow);
    return row_of(
        RowKind::Action,
        "Pair Bluetooth controller",
        tier_,
        app::PageDescription::RowIcon::Bluetooth
    );
}

Transition HdPairRow::select() {
    TASTY_SEAT_BODY(HdPairRow);
    if (!ctx_.launches.script)
        return Transition{};
    ctx_.launches.script->pending = ScriptLaunch::Request::bt_pair(ctx_.bt_reset_before_pair);
    return Transition{Transition::Kind::Push, PageId::ScriptRun};
}

std::optional<RowKey> HdOptionRow::key() const {
    TASTY_SEAT_BODY(HdOptionRow);
    return RowKey::ordinal(item_.item);
}

OptionItem::Look HdOptionRow::look_() const {
    return OptionItem::Look{.flat = false, .value = host_.field(item_.bits)};
}

Row HdOptionRow::describe() const {
    TASTY_SEAT_BODY(HdOptionRow);

    const std::optional<Row> described = item_.describe(look_());
    if (!described)
        return Row{};
    Row r = *described;
    (void)r.label.assign(hd_label(r.label.view()));
    return r;
}

Transition HdOptionRow::select() {
    TASTY_SEAT_BODY(HdOptionRow);
    return item_.select(host_);
}

Transition HdOptionRow::step(CycleDir dir) {
    TASTY_SEAT_BODY(HdOptionRow);
    return item_.step(dir, host_.field(item_.bits), host_);
}

std::optional<RowKey> HdSaveRow::key() const {
    TASTY_SEAT_BODY(HdSaveRow);
    return RowKey::save();
}

Row HdSaveRow::describe() const {
    TASTY_SEAT_BODY(HdSaveRow);
    Row r{};
    r.kind = RowKind::Action;
    (void)r.label.assign("Save settings");
    if (baseline_.unsaved(host_.live_words()))
        (void)r.value.assign("Unsaved");
    return r;
}

Transition HdSaveRow::select() {
    TASTY_SEAT_BODY(HdSaveRow);
    host_.save_config();
    return Transition{};
}

}
