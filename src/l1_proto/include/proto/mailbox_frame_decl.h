// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/mailbox_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

struct MailboxFrameDecl {
    TASTY_SEAT_EXEMPT(component);
    UioOp poll_opcode = UioOp::Status;
    std::uint8_t words = 0;
};

[[nodiscard]] constexpr bool well_formed(const MailboxFrameDecl& d) noexcept {
    return d.words >= 1 && d.words <= kMailboxFrameWords;
}

}
