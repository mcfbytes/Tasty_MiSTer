// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/build_generation.h"
#include "proto/link_pad_encoder.h"
#include "proto/link/pad_relay.h"

namespace mister::proto::link {

class RelayPadEncoder final : public ILinkPadEncoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    RelayPadEncoder(BuildGeneration gen, bool gated, infra::Tally seq = {}) noexcept;

    [[nodiscard]] Ex<void> digital(UioOp opcode, JoyMask map) override;
    [[nodiscard]] Ex<void> stick(UioOp opcode, std::uint8_t index, StickPos pos) override;
    [[nodiscard]] std::uint32_t link_words(UioOp opcode, bool follow_up) const noexcept override;

    void gate(bool gated) noexcept;

    [[nodiscard]] std::optional<PadRelay> take_moved() noexcept;

    [[nodiscard]] const PadRelay& record() const noexcept { return record_; }

    [[nodiscard]] infra::Tally refused() const noexcept { return refused_; }

private:
    [[nodiscard]] Ex<void> level_(UioOp opcode, std::uint8_t index, PadWord word);

    PadRelay record_{};
    bool moved_ = true;
    infra::Tally refused_{};
};

}
