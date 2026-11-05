// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "hal/reg_block.h"
#include "infra/seat.h"
#include "proto/axi_block_decl.h"
#include "proto/link_pad_encoder.h"
#include "proto/pad_levels.h"

namespace mister::proto {

class AxiPadEncoder final : public ILinkPadEncoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    AxiPadEncoder(
        hal::reg::Block pad,
        const AxiBlockDecl& block,
        ILinkPadEncoder& otherwise
    ) noexcept
        : pad_(std::move(pad)),
          block_(block),
          otherwise_(otherwise) {}

    [[nodiscard]] Ex<void> digital(UioOp opcode, JoyMask map) override;

    [[nodiscard]] Ex<void> stick(UioOp opcode, std::uint8_t index, StickPos pos) override;
    [[nodiscard]] std::uint32_t link_words(UioOp opcode, bool follow_up) const noexcept override;

    void trust(bool ready) noexcept;

    [[nodiscard]] bool trusted() const noexcept { return trusted_; }

    [[nodiscard]] std::uint32_t held() const noexcept { return held_; }

private:
    [[nodiscard]] Ex<void> level_(PadLevels::Slot level, std::uint32_t word);
    [[nodiscard]] Ex<void> settle_(PadLevels::Slot level);
    [[nodiscard]] Ex<void> store_(UioOp opcode, std::uint8_t index, std::uint32_t word);

    hal::reg::Block pad_;
    const AxiBlockDecl& block_;
    ILinkPadEncoder& otherwise_;
    PadLevels want_{};
    PadLevels stored_{};
    std::uint32_t held_ = 0;
    bool trusted_ = false;
};

}
