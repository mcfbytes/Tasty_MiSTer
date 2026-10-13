// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/seat.h"

namespace mister::os {

class BlockDevice {
    TASTY_SEAT_EXEMPT(component);

public:
    explicit BlockDevice(int fd) noexcept : fd_(fd) {}

    [[nodiscard]] std::optional<std::uint64_t> size_bytes() const noexcept;

private:
    int fd_ = -1;
};

}  // namespace mister::os
