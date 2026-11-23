// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/message_sum.h"
#include "infra/xthread/inbox.h"
#include "svc/nic_link.h"
#include "svc/nic_personality.h"

namespace mister::svc {

struct NicControl {
    enum class Kind : std::uint8_t { Start, Stop, kCount };
    static constexpr std::size_t kStore = 2 * sizeof(MakeNic);

    struct Start {
        static constexpr Kind kKind = Kind::Start;
        MakeNic make = nullptr;
        NicLink link = NicLink::Off;
        std::array<std::uint8_t, sizeof(MakeNic) - 1> pad_{};
    };

    struct Stop {
        static constexpr Kind kKind = Kind::Stop;
    };

    using Alternatives = std::tuple<Start, Stop>;

    Kind kind = Kind::Start;
    std::array<std::uint8_t, alignof(MakeNic) - 1> pad_{};
    alignas(MakeNic) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<NicControl> && infra::alternatives_are_total<NicControl>());

inline constexpr std::size_t kNicControlDepth = 4;
using NicControlInbox = infra::xthread::Inbox<NicControl, kNicControlDepth>;

}
