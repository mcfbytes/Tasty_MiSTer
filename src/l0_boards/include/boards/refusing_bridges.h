// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "hal/bridge_sequencer.h"
#include "infra/prelude.h"

namespace mister::boards {

class RefusingBridges final : public hal::IBridgeSequencer {
public:
    RefusingBridges() = default;

    [[nodiscard]] Ex<void> pre_program() override;
    [[nodiscard]] Ex<void> post_program() override;
    [[nodiscard]] Ex<void> request_board_reset() override;
};

}
