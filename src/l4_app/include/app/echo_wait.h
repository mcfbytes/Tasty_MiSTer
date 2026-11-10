// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "app/echo_rate.h"
#include "app/file_tx_level.h"
#include "cores/ladder_host.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

class EchoWait final {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Answer : std::uint8_t { Waiting, Answered, Dropped, TimedOut };

    [[nodiscard]] proto::ActKey16 mint(cores::ILadderHost& host) noexcept;

    void arm(
        proto::ActKey16 act,
        std::chrono::nanoseconds now,
        proto::FileSize bytes,
        EchoRate rate
    ) noexcept;

    [[nodiscard]] bool ready(const FileTxLevelCell& level) const noexcept;

    [[nodiscard]] Answer poll(
        const FileTxLevelCell& level,
        std::chrono::nanoseconds now
    ) const noexcept;

private:
    proto::ActKey16 act_{};
    std::chrono::nanoseconds due_{};
};

inline proto::ActKey16 EchoWait::mint(cores::ILadderHost& host) noexcept {
    proto::ActKey16 next{};
    while (!next || next == act_)
        next = host.next_generation().narrow();
    return next;
}

inline void EchoWait::arm(
    proto::ActKey16 act,
    std::chrono::nanoseconds now,
    proto::FileSize bytes,
    EchoRate rate
) noexcept {
    act_ = act;
    due_ = now + echo_bound(bytes, rate);
}

inline bool EchoWait::ready(const FileTxLevelCell& level) const noexcept {
    const auto s = level.sample();
    return s && s.value.payload && s.value.act == act_.widen();
}

inline EchoWait::Answer EchoWait::poll(
    const FileTxLevelCell& level,
    std::chrono::nanoseconds now
) const noexcept {
    const auto s = level.sample();
    if (!s || !s.value.payload || s.value.act != act_.widen())
        return now < due_ ? Answer::Waiting : Answer::TimedOut;
    return s.value.open ? Answer::Answered : Answer::Dropped;
}

}
