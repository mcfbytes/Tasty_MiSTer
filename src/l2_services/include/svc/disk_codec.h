// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

enum class DiskCodec : std::uint8_t {
    Raw,
    kCount,
};

}
