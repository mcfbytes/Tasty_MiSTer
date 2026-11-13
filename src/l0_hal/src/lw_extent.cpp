// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/types.h"

#include <limits>

#include "os/map_length.h"

namespace mister::hal {

std::optional<LwExtent> LwExtent::of_length(os::MapLength len) noexcept {
    if (len.bytes() > std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
    return LwExtent{static_cast<std::uint32_t>(len.bytes())};
}

os::MapLength LwExtent::map_length() const noexcept {
    return os::MapLength{v};
}

}
