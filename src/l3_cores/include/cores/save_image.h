// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

struct SaveImage {
    TASTY_SEAT_EXEMPT(component);
    proto::WideIoIndex index{};
    std::uint32_t size = 0;
    friend constexpr bool operator==(const SaveImage&, const SaveImage&) = default;
};

}
