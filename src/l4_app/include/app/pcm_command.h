// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/message_sum.h"
#include "infra/pool_slot.h"
#include "infra/seat.h"
#include "infra/slot_pool.h"

namespace mister::app {

struct PcmCommand {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t { Play, Stop, Resume, AttachRing, AttachSource, kCount };
    static constexpr std::size_t kStore = 2;

    struct Play {
        static constexpr Kind kKind = Kind::Play;
        std::uint8_t track = 0;
        bool loop = false;
    };

    struct Stop {
        static constexpr Kind kKind = Kind::Stop;
    };

    struct Resume {
        static constexpr Kind kKind = Kind::Resume;
    };

    struct AttachRing {
        static constexpr Kind kKind = Kind::AttachRing;
        xthread::PoolSlot lease{};
    };

    struct AttachSource {
        static constexpr Kind kKind = Kind::AttachSource;
        xthread::PoolSlot lease{};
    };

    using Alternatives = std::tuple<Play, Stop, Resume, AttachRing, AttachSource>;

    Kind kind = Kind::Stop;
    std::uint8_t pad_{};
    std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<PcmCommand> && infra::alternatives_are_total<PcmCommand>());
static_assert(sizeof(PcmCommand) == 4, "the ring element stays one word");

}
