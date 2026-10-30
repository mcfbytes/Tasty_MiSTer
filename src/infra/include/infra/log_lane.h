// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/counted_lane.h"
#include "infra/log_rec.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::xthread {

struct LogKindRow {
    LogRec::Kind kind;
    LogSev sev;
};

inline constexpr std::array<LogKindRow, infra::ordinal(LogRec::Kind::kCount)> kLogKindRows{{
    {LogRec::Kind::DeadlineMiss, LogSev::warn},
    {LogRec::Kind::SessionTransition, LogSev::info},
    {LogRec::Kind::StagingRung, LogSev::info},
    {LogRec::Kind::CoreLoad, LogSev::info},
    {LogRec::Kind::CoreUnload, LogSev::info},
    {LogRec::Kind::Mount, LogSev::info},
    {LogRec::Kind::Unmount, LogSev::info},
    {LogRec::Kind::Refusal, LogSev::warn},
    {LogRec::Kind::RingLoss, LogSev::alarm},
    {LogRec::Kind::DoorbellBound, LogSev::info},
    {LogRec::Kind::DoorbellPolling, LogSev::warn},
    {LogRec::Kind::PhaseTolerated, LogSev::warn},
    {LogRec::Kind::ConfStrArrived, LogSev::info},
}};
static_assert(
    infra::rows_are_ordinal(kLogKindRows),
    "kLogKindRows: exactly one row per LogRec::Kind, in enumerator order"
);

[[nodiscard]] constexpr LogSev sev_of(LogRec::Kind k) noexcept {
    return kLogKindRows[infra::ordinal(k)].sev;
}

inline constexpr std::size_t kLogLaneCap = 256;

struct LogLaneHead {
    template <class A>
    static constexpr LogRec::Head head(std::uint64_t t_ns) noexcept {
        return {sev_of(A::kKind), {}, t_ns};
    }
};

using LogLane = CountedLane<LogRec, kLogLaneCap, SeatTag::RT, SeatTag::Diag, LogLaneHead>;

inline constexpr std::size_t kMainLogLaneCap = 64;
using MainLogLane = CountedLane<LogRec, kMainLogLaneCap, SeatTag::Main, SeatTag::Diag, LogLaneHead>;

}
