// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/front_end_background.h"

#include <linux/input-event-codes.h>

#include "ui/page_ask.h"

namespace mister::ui {

using namespace mister;

void FrontEndBackground::round(
    const StatusView& status,
    bool front_end,
    infra::OptRef<app::UiRequestRing> asks
) noexcept {
    TASTY_SEAT_BODY(FrontEndBackground);
    const proto::FieldValue value = status.field(app::kFrontEndBgField);

    const app::IdleKnobsCell::Sample knobs = knobs_.sample();
    const bool terminal = knobs && knobs.value.fb_terminal;
    bg_ = front_end && terminal ? app::front_end_bg_of(value) : app::FrontEndBg::None;
    if (!pending_)
        return;
    if (value == wanted_) {
        if (push_ask(asks, app::UiRequest::SaveConfig{})) {
            saves_.add();
        } else {
            refusals_.add();
        }
        pending_ = false;
        return;
    }
    ack_rounds_.add();
    if (ack_rounds_ == kF1AckRounds) {
        pending_ = false;
        dropped_.add();
    }
}

bool FrontEndBackground::on_key(
    const app::RawKeyEdge& edge,
    const StatusView& status,
    bool front_end,
    app::LinkTxChannel<SeatTag::Ui>& tx
) noexcept {
    TASTY_SEAT_BODY(FrontEndBackground);
    constexpr svc::EvdevCode kKeyF1{KEY_F1};
    if (!edge.pressed || edge.code != kKeyF1)
        return false;
    const app::IdleKnobsCell::Sample knobs = knobs_.sample();
    if (!front_end || !knobs || !knobs.value.fb_terminal)
        return false;

    const proto::FieldValue wanted = status.field(app::kFrontEndBgField)
                                         .cycled_forward(app::kFrontEndBgField.slots());
    const proto::LinkOp::WriteStatus op{
        .field = app::kFrontEndBgField,
        .value = proto::FieldValue{proto::printed_field(wanted)}
    };
    if (!tx.push(op)) {
        refusals_.add();
        return true;
    }
    wanted_ = wanted;
    pending_ = true;
    ack_rounds_ = svc::Tally{};
    return true;
}

}
