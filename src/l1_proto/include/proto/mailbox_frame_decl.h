// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/mailbox_frame.h"

namespace mister::proto {

struct MailboxFrameDecl {
    std::uint16_t poll_opcode = 0;
    std::uint8_t words = 0;
};

[[nodiscard]] constexpr bool well_formed(const MailboxFrameDecl& d) noexcept {
    return d.poll_opcode != 0 && d.words >= 1 && d.words <= kMailboxFrameWords;
}

}  // namespace mister::proto
