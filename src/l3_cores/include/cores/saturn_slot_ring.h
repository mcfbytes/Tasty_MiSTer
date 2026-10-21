// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::cores {

class SaturnSlotRing {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::uint8_t kSlots = 4;

    [[nodiscard]] std::uint8_t write() const noexcept { return write_; }

    [[nodiscard]] std::uint8_t take_read() noexcept {
        const std::uint8_t at = read_;
        read_ = wrap_(read_ + 1);
        return at;
    }

    void advance_write() noexcept { write_ = wrap_(write_ + 1); }

    void skip_write() noexcept { ++write_; }

    void rewind() noexcept { write_ = read_ = 0; }

private:
    [[nodiscard]] static constexpr std::uint8_t wrap_(unsigned v) noexcept {
        return static_cast<std::uint8_t>(v & (kSlots - 1u));
    }

    std::uint8_t write_ = 0;
    std::uint8_t read_ = 0;
};

}
