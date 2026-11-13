// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "proto/types.h"
#include "proto/window_fails.h"

namespace mister::app {

enum class PreludeStep : std::uint8_t { Idle, Owed, AwaitFltNum, Streaming, AwaitTail, Done };

struct PreludeStats {
    std::uint32_t arms = 0;
    std::uint32_t completions = 0;
    std::uint32_t steps = 0;
    std::uint32_t chunks = 0;
    std::uint32_t coeff_words = 0;
    std::uint32_t parked = 0;
    std::uint32_t barrier_waits = 0;
    std::uint32_t barrier_timeouts = 0;
    std::uint32_t errors = 0;
    std::uint32_t coeff_skips = 0;
    std::uint32_t afilter_skips = 0;
    proto::ReplyWord flt_flags{};
    proto::ReplyWord fb_ack{};
    proto::ReplyWord hdmi_int{};
    proto::ReplyWord gamma_cap{};
    proto::ReplyWord af_flags{};
    proto::ReplyWord sm_cap{};

    proto::WindowFails tail_fail{};
    PreludeStep step = PreludeStep::Idle;
};

static_assert(std::is_trivially_copyable_v<PreludeStats>);

}
