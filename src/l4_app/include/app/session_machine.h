// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/event.h"
#include "app/types.h"
#include "infra/error.h"
#include "infra/log_lane.h"
#include "infra/seat.h"

namespace mister::app {

enum class SessionState : std::uint8_t {
    Boot,
    Running,
    Failed,
};

class SessionMachine {
    TASTY_SEAT_RESIDENT(RT);

public:
    SessionMachine(EventQueue& events, xthread::LogLane& lane) noexcept;

    SessionMachine(const SessionMachine&) = delete;
    SessionMachine& operator=(const SessionMachine&) = delete;

    [[nodiscard]] SessionState state() const noexcept { return state_; }

    void adopt_tag(CorrelationTag t) noexcept { tag_ = t; }

    void transition(SessionState to);

    void advise(const Event::SessionAdvisory& a, EmitSite site);
    void advise(const Event::InfoRequest& a, EmitSite site);
    void advise(const Event::SdActivity& a, EmitSite site);
    void advise(const Event::ProgressUpdate& a, EmitSite site);

    void close(const Event::SessionFailed& a, EmitSite site);
    void close(const Event::CoreLoaded& a, EmitSite site);
    void close(const Event::ConfStrOnlySession& a, EmitSite site);

    void echo(const Event::InfoRequest& a, EmitSite site, CorrelationTag tag);

private:
    EventQueue& events_;
    SessionState state_ = SessionState::Boot;

    CorrelationTag tag_{};

    xthread::LogLane& log_lane_;
    std::uint64_t last_transition_ns_ = 0;
};

}  // namespace mister::app
