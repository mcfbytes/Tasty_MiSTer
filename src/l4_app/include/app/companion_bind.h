// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "app/path_text.h"
#include "cores/mailbox_servants.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::app {

struct CompanionBind {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Attach, Bind, kCount };

    struct Attach {
        static constexpr Kind kKind = Kind::Attach;
        cores::ServantId servant = cores::ServantId::None;
    };

    struct Bind {
        static constexpr Kind kKind = Kind::Bind;
        PathText stem{};
        std::uint16_t gen = 0;
        bool present = false;
        std::uint8_t pad_{};
    };
    static constexpr std::size_t kStore = std::max(sizeof(Attach), sizeof(Bind));

    using Alternatives = std::tuple<Attach, Bind>;

    Kind kind = Kind::Attach;
    std::uint8_t pad_{};
    alignas(alignof(Bind)) std::array<std::byte, kStore> store{};
};
static_assert(infra::MessageSum<CompanionBind> && infra::alternatives_are_total<CompanionBind>());

}  // namespace mister::app
