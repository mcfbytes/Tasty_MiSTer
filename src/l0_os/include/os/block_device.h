// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::os {

class BlockDevice {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static BlockDevice borrow(int fd) noexcept { return BlockDevice{fd}; }

    [[nodiscard]] Ex<std::uint64_t> size_bytes() const noexcept;

private:
    explicit BlockDevice(int fd) noexcept : fd_(fd) {}

    int fd_ = -1;
};

}
