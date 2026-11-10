// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/event.h"
#include "app/log_clock.h"
#include "app/types.h"
#include "infra/error.h"
#include "infra/log_lane.h"
#include "infra/seat.h"

namespace mister::app {

enum class SessionState : std::uint8_t {
    Boot,
    Running,
    Failed,
    kCount,
};

struct SessionArrow {
    SessionState from;
    SessionState to;
};

inline constexpr std::array<SessionArrow, 5> kSessionArrows{{
    {SessionState::Boot, SessionState::Running},
    {SessionState::Boot, SessionState::Failed},
    {SessionState::Running, SessionState::Running},
    {SessionState::Running, SessionState::Failed},
    {SessionState::Failed, SessionState::Failed},
}};

consteval bool session_arrows_are_closed() {
    constexpr std::size_t kStates = static_cast<std::size_t>(SessionState::kCount);
    unsigned sources[kStates] = {};
    for (const SessionArrow& a : kSessionArrows) {
        const auto from = static_cast<std::size_t>(a.from);
        const auto to = static_cast<std::size_t>(a.to);
        if (from >= kStates || to >= kStates)
            return false;
        ++sources[from];
    }
    for (unsigned n : sources) {
        if (n == 0)
            return false;
    }
    for (std::size_t i = 0; i < kSessionArrows.size(); ++i) {
        for (std::size_t j = i + 1; j < kSessionArrows.size(); ++j) {
            if (kSessionArrows[i].from == kSessionArrows[j].from
                && kSessionArrows[i].to == kSessionArrows[j].to)
                return false;
        }
    }
    for (const SessionArrow& a : kSessionArrows) {
        if (a.from == SessionState::Failed && a.to != SessionState::Failed)
            return false;
    }
    return true;
}

static_assert(
    session_arrows_are_closed(),
    "kSessionArrows must leave no state without an edge and keep Failed terminal - "
    "an arrow missing from the table is a transition the machine now REFUSES"
);

[[nodiscard]] constexpr bool session_arrow(SessionState from, SessionState to) noexcept {
    for (const SessionArrow& a : kSessionArrows) {
        if (a.from == from && a.to == to)
            return true;
    }
    return false;
}

class SessionMachine {
    TASTY_SEAT_RESIDENT(RT);

public:
    SessionMachine(EventQueue<SeatTag::RT>& events, xthread::LogLane& lane) noexcept;

    SessionMachine(const SessionMachine&) = delete;
    SessionMachine& operator=(const SessionMachine&) = delete;

    [[nodiscard]] SessionState state() const noexcept { return state_; }

    void adopt_tag(CorrelationTag t) noexcept { tag_ = t; }

    void transition(SessionState to);

    void advise(const Event::SessionAdvisory& a, infra::ErrorSite site);
    void advise(const Event::InfoRequest& a, infra::ErrorSite site);
    void advise(const Event::SdActivity& a, infra::ErrorSite site);
    void advise(const Event::ProgressUpdate& a, infra::ErrorSite site);

    void close(const Event::SessionFailed& a, infra::ErrorSite site);
    void close(const Event::CoreLoaded& a, infra::ErrorSite site);
    void close(const Event::ConfStrOnlySession& a, infra::ErrorSite site);

    void echo(const Event::InfoRequest& a, infra::ErrorSite site, CorrelationTag tag);

private:
    EventQueue<SeatTag::RT>::Producer events_;
    SessionState state_ = SessionState::Boot;

    CorrelationTag tag_{};

    xthread::LogLane::Producer log_lane_;
    LogInstant last_transition_{};
};

}
