// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_machine.h"

#include "app/log_clock.h"

namespace mister::app {

SessionMachine::SessionMachine(EventQueue<SeatTag::RT>& events, xthread::LogLane& lane) noexcept
    : events_(events.producer()),
      log_lane_(lane.producer()),
      last_transition_(log_now()) {}

void SessionMachine::transition(SessionState to) {

    if (!session_arrow(state_, to))
        return;
    const SessionState from = state_;
    state_ = to;
    if (to == SessionState::Failed) {
        events_.push(Event::SessionEnded{}, Event::Head{ERR_SITE(), {}, tag_});
    }

    const LogInstant now = log_now();
    const StatusMicros dwell = us_between(last_transition_, now);
    last_transition_ = now;
    log_lane_.push(
        LogRec::SessionTransition{
            .dwell = dwell,
            .from = static_cast<std::uint8_t>(from),
            .to = static_cast<std::uint8_t>(to)
        },
        log_stamp(now)
    );
}

void SessionMachine::advise(const Event::SessionAdvisory& a, infra::ErrorSite site) {
    events_.push(a, Event::Head{site, {}, kUncaused});
}

void SessionMachine::advise(const Event::InfoRequest& a, infra::ErrorSite site) {
    events_.push(a, Event::Head{site, {}, kUncaused});
}

void SessionMachine::advise(const Event::SdActivity& a, infra::ErrorSite site) {
    events_.push(a, Event::Head{site, {}, kUncaused});
}

void SessionMachine::advise(const Event::ProgressUpdate& a, infra::ErrorSite site) {
    events_.push(a, Event::Head{site, {}, kUncaused});
}

void SessionMachine::close(const Event::SessionFailed& a, infra::ErrorSite site) {
    events_.push(a, Event::Head{site, {}, tag_});
}

void SessionMachine::close(const Event::CoreLoaded& a, infra::ErrorSite site) {
    events_.push(a, Event::Head{site, {}, tag_});
}

void SessionMachine::close(const Event::ConfStrOnlySession& a, infra::ErrorSite site) {
    events_.push(a, Event::Head{site, {}, tag_});
}

void SessionMachine::echo(const Event::InfoRequest& a, infra::ErrorSite site, CorrelationTag tag) {
    events_.push(a, Event::Head{site, {}, tag});
}

}
