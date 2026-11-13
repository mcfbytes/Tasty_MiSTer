// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_switch.h"

#include <unistd.h>

#include <chrono>
#include <cstdio>

namespace mister::app {

bool CoreSwitch::validate(const UiRequest::LoadCore& req) {
    TASTY_SEAT_BODY(CoreSwitch);
    return validate_load(vfs_, req);
}

void CoreSwitch::on_load_core(const UiRequest::LoadCore& req, CorrelationTag tag, Host& host) {
    TASTY_SEAT_BODY(CoreSwitch);
    const auto refuse = [&](Errc code) {
        ++tally_.load_core_refusals;
        publish_refusal_(UiRequest::Kind::LoadCore, code, tag);
    };

    if (req.xml >= XmlKind::Mgl) {
        refuse(Errc::bad_format);
        return;
    }
    if (switch_standing_
        || readiness_.recovering()
        || reboot_.failed()
        || reboot_.owed().has_value()) {
        refuse(Errc::negotiation);
        return;
    }

    const std::optional<HeldLoad> prior = std::exchange(held_load_, std::nullopt);
    const Asked asked = ask(req, tag, host);
    if (asked == Asked::Invalid) {
        held_load_ = prior;
        return;
    }
    if (prior) {
        ++tally_.loads_cancelled;
        publish_refusal_(UiRequest::Kind::LoadCore, Errc::cancelled, prior->tag);
    }

    if (asked == Asked::NoRoom) {
        held_load_.emplace(HeldLoad{.req = req, .tag = tag});
        ++tally_.loads_held;
    }
}

void CoreSwitch::retry_held_load(Host& host) {
    TASTY_SEAT_BODY(CoreSwitch);
    if (!held_load_ || switch_standing_ || readiness_.recovering())
        return;
    if (proto::kLinkTxCapacity - inbox_.ring().size() < kTeardownOps)
        return;
    const HeldLoad h = *held_load_;
    held_load_.reset();
    on_load_core(h.req, h.tag, host);
}

void CoreSwitch::ask_or_refuse(const UiRequest::LoadCore& req, CorrelationTag tag, Host& host) {
    TASTY_SEAT_BODY(CoreSwitch);
    if (ask(req, tag, host) == Asked::NoRoom)
        publish_refusal_(UiRequest::Kind::LoadCore, Errc::would_block, tag);
}

bool CoreSwitch::push_last_order() noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    if (!last_order_)
        return true;
    if (!inbox_.push(*last_order_))
        return false;
    last_order_.reset();
    switch_standing_ = false;
    resume_seats_();
    park_.end();
    return true;
}

void CoreSwitch::pause_seats_(proto::BindGeneration gen, hal::PauseEdge edge) noexcept {
    for (const GrantedSeat& s : seats_)
        if (s.granted() && s.pause_at == edge)
            s.latch->pause(pause_gen_of(gen));
}

void CoreSwitch::resume_seats_() noexcept {
    for (const GrantedSeat& s : seats_)
        if (s.granted())
            s.latch->resume();
}

bool CoreSwitch::seats_paused_(proto::BindGeneration gen) const noexcept {
    for (const GrantedSeat& s : seats_)
        if (s.granted() && !s.latch->paused_at(pause_gen_of(gen)))
            return false;
    return true;
}

bool CoreSwitch::mapping_owner_late_(proto::BindGeneration gen) const noexcept {
    for (const GrantedSeat& s : seats_)
        if (s.granted()
            && s.ddr == hal::DdrMapping::Owner
            && !s.latch->paused_at(pause_gen_of(gen)))
            return true;
    return false;
}

bool CoreSwitch::program_owed() const noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    return receipt_.has_value() && !readiness_.recovering() && seats_paused_(gen_);
}

bool CoreSwitch::receipt_owed() const noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    return switch_standing_ && minted_ != gen_ && park_.answered(pause_gen_of(gen_)).has_value();
}

unsigned CoreSwitch::take_park_acks() {
    TASTY_SEAT_BODY(CoreSwitch);
    if (!switch_standing_ || minted_ == gen_)
        return 0;
    auto receipt = park_.answered(pause_gen_of(gen_));
    if (!receipt)
        return 0;
    minted_ = gen_;

    receipt_.emplace(std::move(*receipt));

    pause_seats_(gen_, hal::PauseEdge::Receipt);
    pause_due_ = os::Deadline::in(clock_, std::chrono::nanoseconds{kPauseDeadlineNs});
    return 1;
}

std::optional<proto::BindGeneration> CoreSwitch::try_program(Host& host) {
    TASTY_SEAT_BODY(CoreSwitch);
    if (!receipt_ || readiness_.recovering())
        return std::nullopt;
    const bool all = seats_paused_(gen_);
    if (!all && !pause_due_.expired(clock_))
        return std::nullopt;

    if (!all && mapping_owner_late_(gen_)) {
        tally_.pause_expiries.add();
        receipt_.reset();
        host.pause_wedged();
        return std::nullopt;
    }

    if (hold_on_lease_())
        return std::nullopt;

    std::optional<AllLeasesHome> home = builder_.leases_home(*receipt_);
    if (!home)
        return std::nullopt;

    if (!all)
        tally_.pause_expiries.add();
    const ParkReceipt r = std::move(*receipt_);
    receipt_.reset();
    (void)::unlink("/tmp/GAMEID");

    (void)::unlink("/tmp/CONFSTR");
    const Programmed p = program_(r, std::move(*home), host);
    pending_step_arg_ = step_arg(p);

    if (p == Programmed::Ok) {
        switch_programmed_ = true;
        readiness_.clear_owed();
    }
    if (p == Programmed::Failed) {
        end(proto::LinkOp::ApplyCore{.loaded = pending_step_arg_.loaded, .gen = gen_});
        return std::nullopt;
    }
    return gen_;
}

bool CoreSwitch::hold_on_lease_() noexcept {

    const bool sector_free = !builder_.sector().mapped()
        || builder_.sector().retire(readiness_.fabric_configured());
    const bool osd_free = !builder_.osd().mapped()
        || builder_.osd().retire(readiness_.fabric_configured());
    const bool sys_free = !builder_.sys().mapped() || builder_.sys().retire();
    if (sector_free && osd_free && sys_free) {
        lease_hold_.reset();
        return false;
    }
    if (!lease_hold_) {
        lease_hold_ = LeaseHold{
            .since = os::Deadline::in(clock_, std::chrono::nanoseconds{kLeaseHoldWarnNs})
        };
        builder_.count_lease_hold(false);
        return true;
    }
    if (!lease_hold_->warned && lease_hold_->since.expired(clock_)) {
        lease_hold_->warned = true;
        builder_.count_lease_hold(true);
        std::fprintf(
            stderr,
            "mister: core switch %s held: an LW lease (T-IO's sector, T-UI's OSD) is "
            "still out; the FPGA is not programmed while it is\n",
            gen_.text().c_str()
        );
        advise_switch_held_();
    }
    return true;
}

CoreSwitch::Programmed CoreSwitch::program_(
    const ParkReceipt& receipt,
    AllLeasesHome home,
    Host& host
) {

    if (pending_.path.empty())
        return Programmed::NotNeeded;

    auto rel = resolve_bitstream(vfs_, pending_.path.view(), pending_.xml);
    if (!rel) {
        tally_.last_program_error = rel.error();
        ++tally_.programs_failed;
        return Programmed::Failed;
    }
    ++tally_.programs_run;
    const Ex<void> took = programmer_.program(receipt, std::move(home), *rel);
    if (!took) {
        tally_.last_program_error = took.error();
        ++tally_.programs_failed;
    }
    host.programmed(took.has_value());
    return took ? Programmed::Ok : Programmed::Failed;
}

CoreSwitch::Programmed CoreSwitch::program_at_boot(Host& host) {
    TASTY_SEAT_BODY(CoreSwitch);
    if (pending_.path.empty())
        return Programmed::NotNeeded;
    auto rel = resolve_bitstream(vfs_, pending_.path.view(), pending_.xml);
    if (!rel) {
        tally_.last_program_error = rel.error();
        ++tally_.programs_failed;
        return Programmed::Failed;
    }

    std::optional<AllLeasesHome> home = builder_.nothing_lent();
    if (!home) {
        ++tally_.programs_failed;
        return Programmed::Failed;
    }
    ++tally_.programs_run;
    const Ex<void> took = programmer_.program_at_boot(std::move(*home), *rel);
    if (!took) {
        tally_.last_program_error = took.error();
        ++tally_.programs_failed;
    }
    host.programmed(took.has_value());
    return took ? Programmed::Ok : Programmed::Failed;
}

CoreSwitch::StepArg CoreSwitch::step_arg(Programmed p) const noexcept {
    TASTY_SEAT_BODY(CoreSwitch);

    if (p == Programmed::NotNeeded)
        return StepArg{.loaded = true};
    return StepArg{.loaded = p == Programmed::Ok};
}

bool CoreSwitch::refuse_in_switch(UiRequest::Kind kind, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    if (reboot_.owed().has_value()) {
        ++owner_tally_.reboot_refusals;
        publish_refusal_(kind, Errc::negotiation, tag);
        return true;
    }
    if (!switch_standing_)
        return false;
    ++tally_.switch_belt_refusals;
    publish_refusal_(kind, Errc::negotiation, tag);
    return true;
}

void CoreSwitch::order_teardown() noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    order_(proto::LinkOp::DropCore{});
    (void)order_refusable_(
        proto::LinkOp::BindDecoders{.table = proto::LinkOp::DecoderTable::Census, .gen = gen_}
    );
    order_(proto::LinkOp::HoldReset{});
}

void CoreSwitch::publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept {
    owner_events_.push(
        Event::RequestRefused{.which = which, .why = code},
        Event::Head{ERR_SITE(), {}, tag}
    );
}

void CoreSwitch::advise_switch_held_() noexcept {
    owner_events_.push(
        Event::SessionAdvisory{.why = Errc::busy},
        Event::Head{ERR_SITE(), {}, kUncaused}
    );
}

void CoreSwitch::advise_switch_failed_(Errc code) noexcept {

    owner_events_.push(Event::SessionAdvisory{.why = code}, Event::Head{ERR_SITE(), {}, kUncaused});
}

}
