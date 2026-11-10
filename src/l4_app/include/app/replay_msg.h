// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "app/types.h"
#include "cores/movie_codec.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::app {

inline constexpr std::size_t kReplayPorts = 4;

inline constexpr proto::IoIndex kNoRomSlot{0xFF};

using ArmMicros = std::chrono::duration<std::uint32_t, std::micro>;
using CodecNs = std::chrono::duration<std::uint32_t, std::nano>;

struct ReplayMsg {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Arm, Input, End, kCount };

    struct Head {
        ControlGeneration gen{};
    };

    static constexpr std::size_t kStore = 24;

    using P0Parity = cores::IReplayCore::Parity;
    using PowerOnEvent = cores::IReplayCore::PowerOnEvent;

    struct Arm {
        static constexpr Kind kKind = Kind::Arm;
        svc::PlayerMask ports{};
        proto::IoIndex rom_index = kNoRomSlot;
        bool vsync_ok = false;
        P0Parity p0 = P0Parity::Any;
        ArmLead lead{};
        PowerOnEvent event = PowerOnEvent::LoadEnd;
        cores::LateFrames late_frames{};
        ArmMicros offset_us{};
        CodecNs period_ns{};
        CodecNs line0_ns{};
        CodecNs poweron_ns{};
    };

    struct Input {
        static constexpr Kind kKind = Kind::Input;
        MovieFrame first{};
        MovieFrame last{};
        std::array<proto::JoyMask, kReplayPorts> mask{};
    };

    struct End {
        static constexpr Kind kKind = Kind::End;
        MovieFrame frame{};
    };

    using Alternatives = std::tuple<Arm, Input, End>;

    Kind kind = Kind::Arm;
    std::uint8_t pad_ = 0;
    Head head{};
    alignas(4) std::array<std::byte, kStore> store{};
};

static_assert(
    infra::MessageSum<ReplayMsg>
    && infra::HasHead<ReplayMsg>
    && infra::alternatives_are_total<ReplayMsg>()
);

static_assert(sizeof(ReplayMsg) == 28, "kind, pad, head and a 24-byte store");

}
