// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/counter.h"
#include "infra/log_rec.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"

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
}};
static_assert(infra::rows_are_ordinal(kLogKindRows),
              "kLogKindRows: exactly one row per LogRec::Kind, in enumerator order");

[[nodiscard]] constexpr LogSev sev_of(LogRec::Kind k) noexcept {
    return kLogKindRows[infra::ordinal(k)].sev;
}

inline constexpr std::size_t kLogLaneCap = 256;

class LogLane {
    TASTY_SEAT_MEDIATOR(Any, Diag);

public:
    template <class A>
        requires infra::AlternativeOf<A, LogRec>
    bool push(const A& alt, std::uint64_t t_ns) noexcept {
        if (ring_.push(infra::make<LogRec>(alt, LogRec::Head{sev_of(A::kKind), {}, t_ns})))
            return true;
        loss_[infra::ordinal(A::kKind)].add();
        return false;
    }

    [[nodiscard]] std::optional<LogRec> pop() noexcept { return ring_.pop(); }

    [[nodiscard]] std::uint32_t losses(LogRec::Kind k) const noexcept {
        return loss_[infra::ordinal(k)].get();
    }

    [[nodiscard]] std::uint32_t total_losses() const noexcept {
        std::uint32_t sum = 0;
        for (const Counter& c : loss_) {
            const std::uint32_t n = c.get();
            sum = (sum > 0xFFFFFFFFu - n) ? 0xFFFFFFFFu : sum + n;
        }
        return sum;
    }

private:
    SpscRing<LogRec, kLogLaneCap> ring_;
    Counter loss_[infra::ordinal(LogRec::Kind::kCount)];
};

}  // namespace mister::xthread
