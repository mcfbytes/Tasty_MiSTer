// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "hal/spi_transport.h"
#include "proto/reset_fence.h"
#include "proto/status_word.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

class StatusRegister {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr unsigned kBits = 128;
    static constexpr unsigned kWords = 8;

    void set_bit(StatusBit bit, bool value);
    bool get_bit(StatusBit bit) const;

    void adopt(std::span<const std::uint16_t, kWords> words) noexcept;

    Ex<void> set_field(const char* spec, std::uint32_t value);

    void fence_with(IResetFence* fence) noexcept { fence_ = fence; }
    Ex<void> flush(hal::ISpiTransport& link);

    bool dirty() const noexcept { return dirty_; }

    hal::SpiWord word(unsigned i) const { return hal::SpiWord{words_[i]}; }

    [[nodiscard]] StatusWord value() const noexcept;

private:
    std::array<std::uint16_t, kWords> words_{};
    bool dirty_ = false;
    StatusWord sent_{};
    IResetFence* fence_ = nullptr;
};

}  // namespace mister::proto
