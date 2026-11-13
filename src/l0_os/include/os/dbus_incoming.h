// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/seat.h"
#include "os/dbus_header.h"

namespace mister::os {

struct DbusIncoming {
    TASTY_SEAT_EXEMPT(component);

    DbusHeader header{};
    std::span<const std::byte> body{};
};

}
