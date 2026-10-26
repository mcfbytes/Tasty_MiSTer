// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/link_pad_encoder.h"
#include "proto/pad_levels.h"

namespace mister::proto {

class HeldPadEncoder final : public ILinkPadEncoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] Ex<void> digital(std::uint16_t opcode, JoyMask map) override;
    [[nodiscard]] Ex<void> stick(
        std::uint16_t opcode,
        std::uint8_t index,
        std::uint8_t x,
        std::uint8_t y
    ) override;

    [[nodiscard]] std::uint32_t link_words(
        std::uint16_t opcode,
        bool follow_up
    ) const noexcept override;

    [[nodiscard]] Ex<void> release(ILinkPadEncoder& to) const;

    [[nodiscard]] std::uint32_t held() const noexcept { return held_; }

private:
    [[nodiscard]] Ex<void> hold_(std::uint16_t opcode, std::uint8_t index, std::uint32_t word);

    PadLevels want_{};
    std::uint32_t held_ = 0;
};

}
