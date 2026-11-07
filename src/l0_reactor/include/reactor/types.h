// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "infra/affine.h"
#include "infra/json_name.h"
#include "infra/json_out.h"

namespace mister::reactor {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

using MonoInstant = std::chrono::steady_clock::time_point;

using RoundSpan = std::chrono::duration<std::uint32_t, std::nano>;

[[nodiscard]] constexpr RoundSpan round_span(std::chrono::nanoseconds d) noexcept {
    if (d <= std::chrono::nanoseconds::zero())
        return RoundSpan::zero();
    if (d >= RoundSpan::max())
        return RoundSpan::max();
    return std::chrono::duration_cast<RoundSpan>(d);
}

struct LeadUpSlot {
    std::uint32_t v = 0;
    friend constexpr bool operator==(LeadUpSlot, LeadUpSlot) = default;
};

struct RoundSeq {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr RoundSeq next() const noexcept { return RoundSeq{v + 1u}; }

    template <std::uint32_t N>
    [[nodiscard]] constexpr LeadUpSlot lead_slot() const noexcept {
        static_assert(N != 0 && (N & (N - 1u)) == 0, "a power-of-two ring");
        return LeadUpSlot{v & (N - 1u)};
    }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, RoundSeq s) noexcept {
        o.field(key, s.v);
    }

    friend constexpr bool operator==(RoundSeq, RoundSeq) = default;
};

}
