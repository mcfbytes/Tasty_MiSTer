// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

enum class WindowShape : std::uint8_t {
    Data,
    Frame,
    Blob,
    Rpc,
};

}
