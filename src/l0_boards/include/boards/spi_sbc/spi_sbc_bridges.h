// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "hal/bridge_sequencer.h"

namespace mister::boards {

class SpiSbcBridges final : public hal::IBridgeSequencer {
public:
    SpiSbcBridges() = default;

    [[nodiscard]] Ex<void> pre_program() override;
    [[nodiscard]] Ex<void> post_program() override;
    [[nodiscard]] Ex<void> request_board_reset() override;
};

}  // namespace mister::boards
