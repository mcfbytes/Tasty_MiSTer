// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "app/uart_modes.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::app {

inline constexpr std::size_t kUartNameMax = 64;

inline constexpr std::size_t kUartHandoffDepth = 8;

struct UartHandoff {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Mode, MidiLink, kCount };
    static constexpr std::size_t kStore = 136;

    struct Mode {
        static constexpr Kind kKind = Kind::Mode;
        std::uint32_t baud = 0;
        char core[kUartNameMax] = {};
        char rbf[kUartNameMax] = {};
        UartMode mode = UartMode::None;
        std::uint8_t pad_[3]{};
    };

    struct MidiLink {
        static constexpr Kind kKind = Kind::MidiLink;
        MidiLinkMode mode = MidiLinkMode::FSynth;
    };

    using Alternatives = std::tuple<Mode, MidiLink>;

    Kind kind = Kind::Mode;
    std::uint8_t pad_[3]{};
    alignas(4) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<UartHandoff> && infra::alternatives_are_total<UartHandoff>());
static_assert(sizeof(UartHandoff) == 140 && alignof(UartHandoff) == 4);

}
