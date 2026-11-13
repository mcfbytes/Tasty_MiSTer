// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/link_event_dispatch.h"
#include "app/ui_round_port.h"
#include "app/video_pump.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link_event.h"

namespace mister::fw {

class UiLinkFan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    UiLinkFan(app::VideoPump& video, infra::OptRef<app::IUiRoundPort> round) noexcept
        : video_(video),
          round_(round) {}

    [[nodiscard]] bool take(const proto::LinkEvent& ev) noexcept {
        TASTY_SEAT_BODY(UiLinkFan);
        return infra::dispatch<app::LinkEventRoutes<app::EventSink::Ui>>(ev, *this, ev);
    }

    [[nodiscard]] std::uint32_t roundless_drops() const noexcept { return roundless_drops_; }

    using Result = bool;

    bool on(const proto::LinkEvent::FrameReply& r, const proto::LinkEvent&) noexcept {
        video_.on_frame_reply(r);
        return false;
    }

    bool on(const proto::LinkEvent::ButtonLevel&, const proto::LinkEvent& whole) noexcept {
        return to_round_(whole);
    }

    bool on(const proto::LinkEvent::OsdMask&, const proto::LinkEvent& whole) noexcept {
        return to_round_(whole);
    }

    bool on(const proto::LinkEvent::PolledWord&, const proto::LinkEvent& whole) noexcept {
        return to_round_(whole);
    }

    bool misrouted(const proto::LinkEvent&, const proto::LinkEvent&) noexcept {
        TASTY_SEAT_BODY(UiLinkFan);
        ++roundless_drops_;
        return false;
    }

private:
    bool to_round_(const proto::LinkEvent& whole) noexcept {
        if (round_)
            return round_->take_link(whole);
        ++roundless_drops_;
        return false;
    }

    app::VideoPump& video_;
    infra::OptRef<app::IUiRoundPort> round_;
    std::uint32_t roundless_drops_ = 0;
};

}
