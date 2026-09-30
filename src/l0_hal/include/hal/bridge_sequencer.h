// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"

namespace mister::hal {

class IBridgeSequencer {
public:
    virtual ~IBridgeSequencer() = default;

    [[nodiscard]] virtual Ex<void> pre_program() = 0;
    [[nodiscard]] virtual Ex<void> post_program() = 0;

    [[nodiscard]] virtual Ex<void> request_board_reset() = 0;

protected:
    IBridgeSequencer() = default;
    IBridgeSequencer(const IBridgeSequencer&) = default;
    IBridgeSequencer& operator=(const IBridgeSequencer&) = default;
};

}  // namespace mister::hal
