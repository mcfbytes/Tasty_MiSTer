// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"

namespace mister::app {

inline constexpr std::size_t kReplayPorts = 4;

struct ReplayMsg {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Arm, Input, End, kCount };
    struct Head {
        std::uint16_t gen = 0;
    };
    static constexpr std::size_t kStore = 24;

    enum class P0Parity : std::uint8_t { Any, Even, Odd, AfterSilence };

    enum class PowerOnEvent : std::uint8_t { LoadEnd, ResetPulse };

    struct Arm {
        static constexpr Kind kKind = Kind::Arm;
        std::uint8_t ports = 0;
        std::uint8_t rom_index = 0xFF;
        std::uint8_t vsync_ok = 0;
        P0Parity p0 = P0Parity::Any;
        std::int16_t lead = 0;
        PowerOnEvent event = PowerOnEvent::LoadEnd;
        std::uint8_t pad_ = 0;
        std::uint32_t offset_us = 0;
        std::uint32_t period_ns = 0;
        std::uint32_t line0_ns = 0;
        std::uint32_t poweron_ns = 0;
    };

    struct Input {
        static constexpr Kind kKind = Kind::Input;
        std::uint32_t first = 0;
        std::uint32_t last = 0;
        std::array<std::uint32_t, kReplayPorts> mask{};
    };

    struct End {
        static constexpr Kind kKind = Kind::End;
        std::uint32_t frame = 0;
    };
    using Alternatives = std::tuple<Arm, Input, End>;

    Kind kind = Kind::Arm;
    std::uint8_t pad_ = 0;
    Head head{};
    alignas(4) std::array<std::byte, kStore> store{};
};
static_assert(infra::MessageSum<ReplayMsg> && infra::HasHead<ReplayMsg> &&
              infra::alternatives_are_total<ReplayMsg>());

static_assert(sizeof(ReplayMsg) == 28, "kind, pad, head and a 24-byte store");

using ReplayRing = xthread::SpscRing<ReplayMsg, 256>;

}  // namespace mister::app
