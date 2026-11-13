// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/uart_modes.h"

namespace mister::app {

struct UartModeFacts {
    BaudSpace space;
    std::optional<UartMode> wire_as;
    std::optional<MidiLinkGroup> admits;
    MidiLinkMode fallback;
};

[[nodiscard]] constexpr UartModeFacts uart_mode_facts(UartMode m) noexcept {
    switch (m) {
        case UartMode::None:
        case UartMode::Ppp:
        case UartMode::Console:
            return {BaudSpace::Uart, std::nullopt, std::nullopt, MidiLinkMode::FSynth};
        case UartMode::Midi:
            return {BaudSpace::Midi, std::nullopt, MidiLinkGroup::Local, MidiLinkMode::FSynth};
        case UartMode::Modem:
            return {BaudSpace::Mlink, UartMode::Ppp, MidiLinkGroup::Network, MidiLinkMode::Tcp};
        case UartMode::Udp:
            return {BaudSpace::Mlink, UartMode::Ppp, std::nullopt, MidiLinkMode::FSynth};
        case UartMode::Sni:
            return {BaudSpace::Mlink, std::nullopt, std::nullopt, MidiLinkMode::FSynth};
    }
    return {BaudSpace::Mlink, std::nullopt, std::nullopt, MidiLinkMode::FSynth};
}

}
