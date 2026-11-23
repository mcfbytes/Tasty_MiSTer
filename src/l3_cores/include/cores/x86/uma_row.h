// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cores/types.h"
#include "proto/types.h"

namespace mister::cores::x86 {

struct UmaRow {

    WindowOffset guest_at{};
    proto::FileSize bytes{};
    std::byte fill = std::byte{0};
    std::string_view rom{};
};

}
