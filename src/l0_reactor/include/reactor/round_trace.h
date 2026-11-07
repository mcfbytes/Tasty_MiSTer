// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "infra/counted_lane.h"
#include "infra/message_sum.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"
#include "reactor/round_timing.h"
#include "reactor/types.h"

namespace mister::reactor {

struct alignas(MonoInstant) RoundTrace {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { SlowSteady, SlowLifecycle, kCount };

    static constexpr std::size_t kMarks = 32;

    static constexpr std::size_t kLeadUp = 8;

    static constexpr std::array kOwnMarks{RoundSegment::Steps, RoundSegment::Tail};
    static_assert(kMarks >= kMaxServices + kOwnMarks.size(), "a full sweep plus Steps and Tail");

    struct Mark {
        RoundTiming::Cause at{};
        RoundSpan took{};
    };

    struct LeadUp {
        MonoInstant start{};
        RoundSpan took{};
        RoundTiming::Cause cause{};
    };

    struct Round {
        MonoInstant start{};
        MonoInstant end{};
        RoundSeq seq{};
        RoundSpan wake{};
        RoundTiming::Cause cause{};
        bool marks_lost = false;
        std::array<std::byte, 3> pad_{};
        std::array<Mark, kMarks> marks{};
        std::array<LeadUp, kLeadUp> lead{};
    };

    struct SlowSteady {
        static constexpr Kind kKind = Kind::SlowSteady;
        Round round{};
    };

    struct SlowLifecycle {
        static constexpr Kind kKind = Kind::SlowLifecycle;
        Round round{};
    };

    static constexpr std::size_t kStore = sizeof(Round);
    using Alternatives = std::tuple<SlowSteady, SlowLifecycle>;

    Kind kind = Kind::SlowSteady;
    std::array<std::byte, 7> pad_{};
    std::array<std::byte, kStore> store{};
};

static_assert(
    std::has_unique_object_representations_v<RoundTrace::Round>,
    "no hidden padding: the lane's sum must be byte-deterministic"
);
static_assert(
    sizeof(RoundTrace::SlowSteady) == RoundTrace::kStore
    && sizeof(RoundTrace::SlowLifecycle) == RoundTrace::kStore
);
static_assert(infra::MessageSum<RoundTrace> && infra::alternatives_are_total<RoundTrace>());

inline constexpr std::size_t kRoundTraceLaneCap = 16;
using RoundTraceLane = xthread::
    CountedLane<RoundTrace, kRoundTraceLaneCap, SeatTag::RT, SeatTag::Diag>;

}
