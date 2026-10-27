// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "hal/types.h"
#include "infra/opt_ref.h"
#include "proto/link_frame.h"
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

    [[nodiscard]] Ex<void> set_field(const char* spec, std::uint32_t value);

    void fence_with(IResetFence* fence) noexcept { fence_ = infra::opt_ref_of(fence); }

    [[nodiscard]] Ex<void> flush(ILinkFrame& frame);

    bool dirty() const noexcept { return dirty_; }

    hal::SpiWord word(unsigned i) const { return hal::SpiWord{words_[i]}; }

    [[nodiscard]] StatusWord value() const noexcept;

private:
    std::array<std::uint16_t, kWords> words_{};
    bool dirty_ = false;
    StatusWord sent_{};
    infra::OptRef<IResetFence> fence_{};
};

}
