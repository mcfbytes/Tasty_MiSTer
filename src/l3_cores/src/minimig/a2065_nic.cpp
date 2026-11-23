// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/a2065_nic.h"

#include "cores/minimig/a2065_mac.h"

namespace mister::cores::minimig {

A2065Nic::A2065Nic(const svc::NicGrant& grant) noexcept
    : window_(grant.window),
      port_(grant.port),
      plan_(svc::NicLinkPlan::of(grant.link)),
      commands_(window_),
      regs_(window_, *this),
      rings_(regs_, window_, munge_, port_) {}

Ex<void> A2065Nic::start() {
    TASTY_SEAT_BODY(A2065Nic);
    if (!plan_)
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    const svc::NicLinkPlan& plan = *plan_;
    commands_.resync();
    window_.store_shadow({});
    window_.store_interrupt(false);
    regs_.reset();

    std::optional<svc::MacAddress> carried;
    if (plan.delivery == svc::NicLinkPlan::Delivery::Direct) {
        if (const auto own = port_.iface_mac(plan.iface))
            carried = A2065Mac::from(*own);
    } else if (plan.delivery == svc::NicLinkPlan::Delivery::Macvlan) {
        if (const auto parent = port_.iface_mac(plan.parent))
            carried = A2065Mac::from(*parent);
    }
    port_.prepare(plan, carried);
    offline_ = !port_.open(plan).has_value();

    const A2065Mac mac = A2065Mac::resolve(
        port_.host_mac(),
        port_.iface_mac(svc::NicLinkPlan::kOnboard),
        port_.host_seed()
    );
    munge_.set(mac);
    regs_.set_station(mac.station);
    if (plan.shared()) {
        port_.filter(munge_.wire());
        prom_active_ = false;
    }
    window_.store_station(regs_.station());
    push_shadow_();
    return {};
}

void A2065Nic::reset() noexcept {
    TASTY_SEAT_BODY(A2065Nic);
    commands_.resync();
    window_.store_shadow({});
    window_.store_interrupt(false);
    regs_.reset();
    push_shadow_();
}

svc::NicPoll A2065Nic::poll() noexcept {
    TASTY_SEAT_BODY(A2065Nic);
    svc::NicPoll pass{};
    sent_this_pass_ = infra::Tally{};
    const A2065CommandRing::Pending owed = commands_.pending();
    if (owed.overran)
        pass.overruns.add();
    if (owed.entries != A2065CommandCount{}) {
        for (A2065CommandCount n{}; n < owed.entries; n += A2065CommandCount{1}) {
            const A2065Command c = commands_.take();
            regs_.select(c.rap());
            regs_.write(c.data());
        }
        pass.commands.add(owed.entries.tally());
        push_shadow_();
        follow_promiscuous_();
    }
    pass.frames_in.add(drain_rx_());
    pass.frames_out = sent_this_pass_;
    return pass;
}

void A2065Nic::stop() noexcept {
    TASTY_SEAT_BODY(A2065Nic);
    window_.store_interrupt(false);
    port_.close();
}

void A2065Nic::follow_promiscuous_() noexcept {
    if (!plan_ || !plan_->shared())
        return;
    const bool want = regs_.promiscuous();
    if (want == prom_active_)
        return;

    port_.filter(want ? std::nullopt : std::optional<svc::MacAddress>{munge_.wire()});
    prom_active_ = want;
}

infra::Tally A2065Nic::drain_rx_() noexcept {
    if (!rings_.rx_has_space())
        return infra::Tally{};

    const bool loop = regs_.loopback();
    infra::Tally taken{};
    batching_ = true;
    while (taken != kRxBatch && (loop || rings_.rx_has_space())) {
        const std::span<std::byte> got = port_.receive(frame_.receive_area());
        if (got.empty())
            break;
        frame_.received(infra::ByteCount{got.size()});
        if (!loop)
            rings_.receive(frame_.view());
        taken.add();
    }
    batching_ = false;
    if (taken != infra::Tally{})
        push_shadow_();
    return taken;
}

void A2065Nic::push_shadow_() noexcept {
    window_.store_shadow(regs_.shadow());
    window_.store_interrupt(regs_.interrupt_line());
}

void A2065Nic::initialised(const svc::MacAddress& station) noexcept {
    TASTY_SEAT_BODY(A2065Nic);
    rings_.reset_loopback_count();
    munge_.set_station(station);
}

void A2065Nic::transmit_demand() noexcept {
    TASTY_SEAT_BODY(A2065Nic);
    if (rings_.transmit())
        sent_this_pass_.add();
    push_shadow_();
}

void A2065Nic::interrupt() noexcept {
    TASTY_SEAT_BODY(A2065Nic);
    if (batching_)
        return;
    push_shadow_();
}

std::unique_ptr<svc::INicPersonality> make_a2065_nic(const svc::NicGrant& grant) {
    return std::make_unique<A2065Nic>(grant);
}

}
