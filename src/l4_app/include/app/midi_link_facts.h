// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/uart_modes.h"

namespace mister::app {

struct MidiLinkFacts {
    MidiLinkGroup group;
    std::optional<std::string_view> sentinel;
};

[[nodiscard]] constexpr MidiLinkFacts midi_link_facts(MidiLinkMode m) noexcept {
    switch (m) {
        case MidiLinkMode::FSynth:
            return {MidiLinkGroup::Local, std::string_view{"/tmp/ML_FSYNTH"}};
        case MidiLinkMode::Munt:
            return {MidiLinkGroup::Local, std::string_view{"/tmp/ML_MUNT"}};
        case MidiLinkMode::UsbMidi:
            return {MidiLinkGroup::Local, std::string_view{"/tmp/ML_USBMIDI"}};
        case MidiLinkMode::Udp:
            return {MidiLinkGroup::Local, std::string_view{"/tmp/ML_UDP"}};
        case MidiLinkMode::Tcp:
            return {MidiLinkGroup::Network, std::string_view{"/tmp/ML_TCP"}};
        case MidiLinkMode::UdpAlt:
            return {MidiLinkGroup::Network, std::string_view{"/tmp/ML_UDP_ALT"}};
        case MidiLinkMode::UsbSer:
            return {MidiLinkGroup::Network, std::string_view{"/tmp/ML_USBSER"}};
    }
    return {MidiLinkGroup::Neither, std::nullopt};
}

}
