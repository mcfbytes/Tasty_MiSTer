// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/bt_pair_plan.h"

#include <optional>
#include <utility>

#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "os/bt_kind.h"
#include "svc/bt_page_mode.h"
#include "svc/connect_request.h"
#include "ui/connect_text.h"
#include "ui/hd_plan_inputs.h"
#include "ui/slot_range.h"
#include "ui/page_context.h"

namespace mister::ui {
namespace {

using RowKind = app::PageDescription::RowKind;
using Request = svc::ConnectRequest;
using Ask = BtPromptRows::Ask;

enum class Slot : std::uint8_t { Headline, First, Second, Cancel, Count };
constexpr SlotRange kDevices = SlotRange::after_named<Slot>(RowCount::of(svc::BtDeviceView::kRows));
constexpr SlotRange kShowAll = SlotRange::after(kDevices, kOneRow);

infra::OptRef<const svc::BtDeviceViewRow> device_at(const ConnectViews& views, SlotId slot) {
    const std::optional<std::size_t> at = kDevices.subscript_of(slot);
    if (!views.devices || !at)
        return std::nullopt;
    const auto& row = views.devices->rows[*at];
    if (!row)
        return {};
    return infra::OptRef<const svc::BtDeviceViewRow>{*row};
}

std::string_view first_label(Ask ask) {
    switch (ask) {
        case Ask::YesNo:
            return "Yes";
        case Ask::AcceptReject:
            return "Accept";
        case Ask::EnterPasskey:
            return "Enter passkey";
        case Ask::EnterPin:
            return "Enter PIN";
        case Ask::None:
            break;
    }
    return {};
}

}

BtPairPlan::BtPairPlan(const HdPlanInputs& in)
    : ConnectSlotPlan(in, kShowAll.total()),
      start_(in.arg.pair_start().value_or(PairStart::Pick)) {
    TASTY_SEAT_BODY(BtPairPlan);

    if (in.ctx.bt_reset_before_pair)
        (void)send(infra::make<Request>(Request::BtResetAdapter{}));
}

PageId BtPairPlan::id() const {
    TASTY_SEAT_BODY(BtPairPlan);
    return PageId::BtPair;
}

std::string_view BtPairPlan::title() const {
    TASTY_SEAT_BODY(BtPairPlan);
    return "Pair controller";
}

bool BtPairPlan::remembers_focus() const noexcept {
    TASTY_SEAT_BODY(BtPairPlan);
    return false;
}

ConnectDemand BtPairPlan::connect_demand() const {
    TASTY_SEAT_BODY(BtPairPlan);
    return ConnectDemand::bluetooth(
        show_all_ ? svc::BtPageMode::PairingAll : svc::BtPageMode::Pairing,
        start_
    );
}

void BtPairPlan::on_focus_moved() {
    TASTY_SEAT_BODY(BtPairPlan);
    start_ = PairStart::Pick;
}

bool BtPairPlan::holds_pairing() const {
    TASTY_SEAT_BODY(BtPairPlan);
    return true;
}

std::optional<BtPromptRows> BtPairPlan::prompt_() const {
    if (!views().prompt || !views().prompt->prompt)
        return std::nullopt;
    return BtPromptRows::of(*views().prompt->prompt);
}

bool BtPairPlan::slot_shown(SlotId slot) const {
    TASTY_SEAT_BODY(BtPairPlan);
    const std::optional<BtPromptRows> prompt = prompt_();
    if (slot.is(Slot::Headline))
        return true;
    if (slot.is(Slot::First))
        return prompt && prompt->ask != Ask::None;
    if (slot.is(Slot::Second))
        return prompt && (prompt->ask == Ask::YesNo || prompt->ask == Ask::AcceptReject);
    if (slot.is(Slot::Cancel))
        return prompt.has_value();
    if (prompt)
        return false;
    if (slot == kShowAll.first())
        return true;
    infra::OptRef<const svc::BtDeviceViewRow> row = device_at(views(), slot);
    return row.has_value() && (show_all_ || os::bt_kind_is_input(row->kind));
}

app::PageDescription::Row BtPairPlan::slot_describe(SlotId slot) const {
    TASTY_SEAT_BODY(BtPairPlan);
    const std::optional<BtPromptRows> prompt = prompt_();
    switch (slot.named<Slot>()) {
        case Slot::Headline:
            if (prompt)
                return connect_row(RowKind::Value, prompt->headline, prompt->value);
            return connect_row(RowKind::Value, "Bluetooth", bt_cell(views().bt).view());
        case Slot::First:
            return connect_row(
                RowKind::Action,
                prompt ? first_label(prompt->ask) : std::string_view{}
            );
        case Slot::Second:
            return connect_row(
                RowKind::Action,
                prompt && prompt->ask == Ask::YesNo ? "No" : "Reject"
            );
        case Slot::Cancel:
            return connect_row(RowKind::Action, "Cancel pairing");
        case Slot::Count:
            break;
    }
    if (slot == kShowAll.first())
        return connect_row(RowKind::Value, "Show all devices", show_all_ ? "On" : "Off");
    infra::OptRef<const svc::BtDeviceViewRow> row = device_at(views(), slot);
    if (!row)
        return connect_row(RowKind::Action, {});
    const std::string_view mark = row->connected ? "connected" : (row->paired ? "paired" : "");
    const std::string name{row->name.empty() ? row->addr.format().view() : row->name.view()};
    return connect_row(RowKind::Value, name, mark);
}

bool BtPairPlan::answer_(svc::PromptAnswer answer) {
    const std::optional<BtPromptRows> prompt = prompt_();
    if (!prompt)
        return false;
    Request::BtAnswer reply{};
    reply.gen = prompt->gen;
    reply.answer = answer;
    return send(infra::make<Request>(reply));
}

Transition BtPairPlan::slot_select(SlotId slot) {
    TASTY_SEAT_BODY(BtPairPlan);
    const std::optional<BtPromptRows> prompt = prompt_();
    switch (slot.named<Slot>()) {
        case Slot::Headline:
            return Transition{};
        case Slot::First:
            if (!prompt)
                return Transition{};

            if (prompt->ask == Ask::EnterPasskey) {
                entry_gen_ = prompt->gen;
                return request_text(slot, TextEntryKind::Passkey, "Passkey", false);
            }
            if (prompt->ask == Ask::EnterPin) {
                entry_gen_ = prompt->gen;
                return request_text(slot, TextEntryKind::Pin, "PIN", false);
            }
            (void)answer_(svc::PromptAnswer::Accept);
            return Transition{};
        case Slot::Second:
            (void)answer_(svc::PromptAnswer::Reject);
            return Transition{};
        case Slot::Cancel:
            (void)answer_(svc::PromptAnswer::Cancel);
            return Transition{};
        case Slot::Count:
            break;
    }
    if (slot == kShowAll.first()) {
        show_all_ = !show_all_;
        return Transition{};
    }
    if (infra::OptRef<const svc::BtDeviceViewRow> row = device_at(views(), slot)) {
        start_ = PairStart::Pick;
        (void)send(infra::make<Request>(Request::BtPair{row->addr}));
    }
    return Transition{};
}

Transition BtPairPlan::slot_entered(SlotId slot, const TextEntryResult& result) {
    TASTY_SEAT_BODY(BtPairPlan);
    (void)slot;
    const std::optional<BtPromptRows> prompt = prompt_();
    const std::optional<svc::PromptGen> opened = std::exchange(entry_gen_, std::nullopt);
    if (!prompt || !opened || prompt->gen != *opened)
        return Transition::pop();
    Request::BtAnswer reply{};
    reply.gen = *opened;
    reply.answer = svc::PromptAnswer::Accept;
    if (const auto key = infra::as<TextEntryResult::Passkey>(result))
        reply.passkey = key->value;
    if (const auto pin = infra::as<TextEntryResult::Pin>(result))
        reply.pin = pin->value;

    return send(infra::make<Request>(reply)) ? Transition::pop() : Transition{};
}

}
