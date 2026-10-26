// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "proto/types.h"

namespace mister::proto {

class ILinkPadEncoder {
public:
    virtual ~ILinkPadEncoder() = default;

    [[nodiscard]] virtual Ex<void> digital(std::uint16_t opcode, JoyMask map) = 0;

    [[nodiscard]] virtual Ex<void> stick(
        std::uint16_t opcode,
        std::uint8_t index,
        std::uint8_t x,
        std::uint8_t y
    ) = 0;

    [[nodiscard]] virtual std::uint32_t link_words(
        std::uint16_t opcode,
        bool follow_up
    ) const noexcept = 0;

protected:
    ILinkPadEncoder() = default;
    ILinkPadEncoder(const ILinkPadEncoder&) = default;
    ILinkPadEncoder& operator=(const ILinkPadEncoder&) = default;
};

}
