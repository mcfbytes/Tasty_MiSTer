// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/joystick.h"
#include "proto/link_frame.h"
#include "proto/link_pad_encoder.h"

namespace mister::proto {

class FramePadEncoder final : public ILinkPadEncoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    FramePadEncoder(ILinkFrame& frame, const JoystickPort& port) noexcept
        : frame_(frame),
          port_(port) {}

    explicit FramePadEncoder(const JoystickPort& port) noexcept : port_(port) {}

    void over(ILinkFrame& frame) noexcept { frame_ = infra::OptRef<ILinkFrame>{frame}; }

    void clear_frame() noexcept { frame_.reset(); }

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
    infra::OptRef<ILinkFrame> frame_{};
    const JoystickPort& port_;
};

}
