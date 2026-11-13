// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/hd_osd_ask.h"
#include "app/idle_knobs.h"
#include "app/link_tx_channel.h"
#include "app/raw_key_edge.h"
#include "app/ui_request_ring.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/types.h"
#include "ui/status_view.h"

namespace mister::ui {

class FrontEndBackground {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr svc::Tally kF1AckRounds{50};

    explicit FrontEndBackground(const app::IdleKnobsCell& idle) noexcept : knobs_(idle) {}

    void round(
        const StatusView& status,
        bool front_end,
        infra::OptRef<app::UiRequestRing> asks
    ) noexcept;

    bool on_key(
        const app::RawKeyEdge& edge,
        const StatusView& status,
        bool front_end,
        app::LinkTxChannel<SeatTag::Ui>& tx
    ) noexcept;

    [[nodiscard]] app::FrontEndBg bg() const noexcept { return bg_; }

    [[nodiscard]] bool pending() const noexcept { return pending_; }

    [[nodiscard]] svc::Tally f1_saves() const noexcept { return saves_; }

    [[nodiscard]] svc::Tally f1_save_dropped() const noexcept { return dropped_; }

    [[nodiscard]] svc::Tally refusals() const noexcept { return refusals_; }

private:
    const app::IdleKnobsCell& knobs_;
    app::FrontEndBg bg_ = app::FrontEndBg::None;

    proto::FieldValue wanted_{};
    bool pending_ = false;
    svc::Tally ack_rounds_{};
    svc::Tally saves_{};
    svc::Tally dropped_{};
    svc::Tally refusals_{};
};

}
