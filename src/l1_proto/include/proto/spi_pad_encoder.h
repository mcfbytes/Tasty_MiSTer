// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/spi_transport.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/joystick.h"
#include "proto/link_pad_encoder.h"

namespace mister::proto {

class SpiPadEncoder final : public ILinkPadEncoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    SpiPadEncoder(hal::ISpiTransport& link, const JoystickPort& port) noexcept
        : link_(link),
          port_(port) {}

    explicit SpiPadEncoder(const JoystickPort& port) noexcept : port_(port) {}

    void over(hal::ISpiTransport& link) noexcept {
        link_ = infra::OptRef<hal::ISpiTransport>{link};
    }

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

private:
    infra::OptRef<hal::ISpiTransport> link_{};
    const JoystickPort& port_;
};

}
