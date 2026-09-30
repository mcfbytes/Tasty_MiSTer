// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

#include "svc/map_store.h"

namespace mister::svc {

struct ButtonMap {
    MapKind kind = MapKind::Joystick;
    std::vector<std::byte> blob;
};

}  // namespace mister::svc
