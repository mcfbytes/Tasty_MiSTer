// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_machine.h"

#include <ctime>

namespace mister::app {

namespace {

std::uint64_t log_now_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

}  // namespace

void SessionMachine::set_log_lane(xthread::LogLane* lane) noexcept {
    log_lane_ = lane;
    last_transition_ns_ = log_now_ns();
}

void SessionMachine::transition(SessionState to) {
    const SessionState from = state_;
    state_ = to;
    if (to == SessionState::Failed) {
        (void)events_->push(
            infra::make<Event>(Event::SessionEnded{}, Event::Head{EmitSite{ERR_SITE()}, {}, tag_}));
    }

    if (log_lane_ != nullptr) {
        const std::uint64_t now = log_now_ns();
        const std::uint64_t dwell_us = (now - last_transition_ns_) / 1000u;
        last_transition_ns_ = now;
        (void)log_lane_->push(
            LogRec::SessionTransition{.dwell_us = dwell_us > 0xFFFFFFFFull
                                                      ? 0xFFFFFFFFu
                                                      : static_cast<std::uint32_t>(dwell_us),
                                      .from = static_cast<std::uint8_t>(from),
                                      .to = static_cast<std::uint8_t>(to)},
            now);
    }
}

void SessionMachine::advise(const Event::SessionAdvisory& a, EmitSite site) {
    (void)events_->push(infra::make<Event>(a, Event::Head{site, {}, kUncaused}));
}

void SessionMachine::advise(const Event::InfoRequest& a, EmitSite site) {
    (void)events_->push(infra::make<Event>(a, Event::Head{site, {}, kUncaused}));
}

void SessionMachine::advise(const Event::SdActivity& a, EmitSite site) {
    (void)events_->push(infra::make<Event>(a, Event::Head{site, {}, kUncaused}));
}

void SessionMachine::advise(const Event::ProgressUpdate& a, EmitSite site) {
    (void)events_->push(infra::make<Event>(a, Event::Head{site, {}, kUncaused}));
}

void SessionMachine::close(const Event::SessionFailed& a, EmitSite site) {
    (void)events_->push(infra::make<Event>(a, Event::Head{site, {}, tag_}));
}

void SessionMachine::close(const Event::CoreLoaded& a, EmitSite site) {
    (void)events_->push(infra::make<Event>(a, Event::Head{site, {}, tag_}));
}

void SessionMachine::close(const Event::ConfStrOnlySession& a, EmitSite site) {
    (void)events_->push(infra::make<Event>(a, Event::Head{site, {}, tag_}));
}

void SessionMachine::echo(const Event::InfoRequest& a, EmitSite site, CorrelationTag tag) {
    (void)events_->push(infra::make<Event>(a, Event::Head{site, {}, tag}));
}

}  // namespace mister::app
