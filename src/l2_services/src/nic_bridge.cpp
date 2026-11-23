// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/nic_bridge.h"

#include "infra/message_sum.h"
#include "svc/nic_control_dispatch.h"
#include "svc/nic_link_plan.h"

namespace mister::svc {

static_assert(NicControlSink<NicBridge>);

NicBridge::NicBridge(const Wiring& w) noexcept
    : inbox_(w.inbox),
      window_(w.window),
      port_(w.port),
      cell_(w.status),
      row_(w.row) {}

NicBridge::~NicBridge() = default;

void NicBridge::serve() noexcept {
    TASTY_SEAT_BODY(NicBridge);
    bool changed = false;
    while (const auto m = inbox_.pop()) {
        infra::dispatch<NicControlRoutes>(*m, *this);
        changed = true;
    }
    if (card_) {
        status_.totals = card_->poll().after(status_.totals);
        changed = true;
    }
    if (changed)
        publish_();
}

void NicBridge::on(const NicControl::Start& s) noexcept {
    TASTY_SEAT_BODY(NicBridge);

    if (card_) {
        card_->reset();
        return;
    }
    if (s.link == NicLink::Off || s.make == nullptr)
        return;

    if (!row_.measured) {
        status_.refused_unmeasured.add();
        return;
    }
    if (!port_.supports(s.link)) {
        status_.refused_link.add();
        return;
    }
    if (auto opened = window_.open(); !opened) {
        status_.start_failures.add();
        return;
    }
    card_ = s.make(NicGrant{.window = window_.window(), .port = port_, .link = s.link});
    if (!card_ || !card_->start()) {
        if (card_)
            card_->stop();
        card_.reset();
        window_.close();
        status_.start_failures.add();
        return;
    }
    status_.link = s.link;
    status_.up = true;
}

void NicBridge::on(const NicControl::Stop&) noexcept {
    TASTY_SEAT_BODY(NicBridge);
    stop_card();
}

void NicBridge::misrouted(const NicControl&) noexcept {
    TASTY_SEAT_BODY(NicBridge);
    fatal(Error{Errc::contract, ERR_SITE(), 0}, "NicControl misrouted");
}

void NicBridge::stop_card() noexcept {
    TASTY_SEAT_BODY(NicBridge);
    if (!card_)
        return;
    card_->stop();
    card_.reset();
    window_.close();
    status_.link = NicLink::Off;
    status_.up = false;
    publish_();
}

void NicBridge::publish_() noexcept {
    cell_.publish(status_);
}

}
