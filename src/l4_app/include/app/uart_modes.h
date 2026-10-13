// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

enum class UartMode : std::uint8_t {
    None = 0,
    Ppp = 1,
    Console = 2,
    Midi = 3,
    Modem = 4,
    Udp = 5,
    Sni = 6,
};

enum class MidiLinkMode : std::uint8_t {
    FSynth = 0,
    Munt = 1,
    UsbMidi = 2,
    Udp = 3,
    Tcp = 4,
    UdpAlt = 5,
    UsbSer = 6,
};

}  // namespace mister::app
