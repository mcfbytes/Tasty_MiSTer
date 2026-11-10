// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/seat.h"

namespace mister::cores {

class SaturnSlotRing {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::uint8_t kSlots = 4;

    struct Slot {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr std::size_t byte_offset() const noexcept {
            constexpr std::size_t kSlotBytes = 4096;
            return static_cast<std::size_t>(v) * kSlotBytes;
        }

        [[nodiscard]] constexpr Slot next() const noexcept {
            return Slot{static_cast<std::uint8_t>((v + 1u) % kSlots)};
        }

        [[nodiscard]] constexpr std::uint8_t mode_nibble() const noexcept { return v; }

        friend constexpr bool operator==(Slot, Slot) = default;
    };

    [[nodiscard]] Slot write() const noexcept { return write_; }

    [[nodiscard]] Slot take_read() noexcept {
        const Slot at = read_;
        read_ = read_.next();
        return at;
    }

    void advance_write() noexcept { write_ = write_.next(); }

    void skip_write() noexcept { write_ = write_.next(); }

    void rewind() noexcept { write_ = read_ = Slot{}; }

private:
    Slot write_{};
    Slot read_{};
};

}
