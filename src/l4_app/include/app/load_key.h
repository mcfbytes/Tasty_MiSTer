// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/seat.h"
#include "svc/types.h"

namespace mister::app {

class LoadKey final {
    TASTY_SEAT_EXEMPT(component);

public:
    explicit LoadKey(std::uint64_t total) noexcept;

    void feed(std::span<const std::uint8_t> bytes) noexcept;

    [[nodiscard]] svc::Crc32 value() const noexcept { return crc_; }

private:
    std::uint64_t skip_;
    svc::Crc32 crc_{};
};

}
