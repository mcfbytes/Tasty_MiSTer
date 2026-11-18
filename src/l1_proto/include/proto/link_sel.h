// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

enum class LinkSel : std::uint8_t { Io, Fpga, Osd, OsdHdmi, OsdVga };

}
