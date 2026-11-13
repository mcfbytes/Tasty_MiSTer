// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>

#include "app/replay_msg.h"
#include "app/types.h"
#include "cores/types.h"

namespace mister::app {

class EpochRule {
public:
    struct Pick {
        CoreFrameGap j{};
        CoreFrameGap hi{};

        [[nodiscard]] constexpr bool ambiguous() const noexcept { return j != hi; }
    };

    static constexpr std::chrono::nanoseconds kEndSlackNs{50'000};

    std::chrono::nanoseconds poweron{};
    std::chrono::nanoseconds period{};
    std::chrono::nanoseconds line0{};
    ReplayMsg::P0Parity p0 = ReplayMsg::P0Parity::Any;
    cores::LateFrames late_frames{};

    [[nodiscard]] constexpr Pick pick(
        std::chrono::nanoseconds delay,
        CountParity first,
        std::chrono::nanoseconds tick
    ) const noexcept {
        const std::chrono::nanoseconds x = poweron + period - line0 - delay;
        return {
            released(ceil_div(x - kEndSlackNs, period), first),
            released(ceil_div(x + tick, period), first)
        };
    }

    [[nodiscard]] constexpr std::optional<CoreFrameCount> slip(
        std::chrono::nanoseconds delay,
        CountParity first,
        std::chrono::nanoseconds tick
    ) const noexcept {
        if (p0 != ReplayMsg::P0Parity::Any)
            return std::nullopt;
        for (const unsigned k : late_frames.slips()) {
            const CoreFrameCount n{k};
            const Pick s = pick(delay - n.lasting(period), first, tick);
            if (!s.ambiguous() && s.j == CoreFrameGap::of_count(n))
                return n;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool by_silence() const noexcept {
        return p0 == ReplayMsg::P0Parity::AfterSilence;
    }

    [[nodiscard]] constexpr bool needs_counter() const noexcept {
        return p0 != ReplayMsg::P0Parity::Any;
    }

private:
    [[nodiscard]] static constexpr CoreFrameGap ceil_div(
        std::chrono::nanoseconds a,
        std::chrono::nanoseconds b
    ) noexcept {
        const CoreFrameGap whole{a / b};
        return a % b == std::chrono::nanoseconds::zero() || a < std::chrono::nanoseconds::zero()
            ? whole
            : whole + CoreFrameGap{1};
    }

    [[nodiscard]] constexpr CoreFrameGap released(
        CoreFrameGap j,
        CountParity first
    ) const noexcept {
        if (p0 == ReplayMsg::P0Parity::Any || j < CoreFrameGap{})
            return j;
        return first.after(j) == CountParity{p0 == ReplayMsg::P0Parity::Odd} ? j
                                                                             : j + CoreFrameGap{1};
    }
};

}
