// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace mister::proto {

struct Capability {
    enum class Kind : std::uint8_t { SaveState, Uart, Midi, Irq, Axi, Unknown };
    Kind kind;
    std::string raw;
};

}
