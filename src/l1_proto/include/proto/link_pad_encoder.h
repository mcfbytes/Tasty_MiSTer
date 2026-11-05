// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "proto/stick_pos.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

class ILinkPadEncoder {
public:
    virtual ~ILinkPadEncoder() = default;

    [[nodiscard]] virtual Ex<void> digital(UioOp opcode, JoyMask map) = 0;

    [[nodiscard]] virtual Ex<void> stick(UioOp opcode, std::uint8_t index, StickPos pos) = 0;

    [[nodiscard]] virtual std::uint32_t link_words(UioOp opcode, bool follow_up) const noexcept = 0;

protected:
    ILinkPadEncoder() = default;
    ILinkPadEncoder(const ILinkPadEncoder&) = default;
    ILinkPadEncoder& operator=(const ILinkPadEncoder&) = default;
};

}
