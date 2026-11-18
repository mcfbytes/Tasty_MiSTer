// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "hal/block_view.h"
#include "hal/pad_block.h"
#include "hal/reg_block.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/link_pad_encoder.h"
#include "proto/pad_block_decl.h"
#include "proto/pad_levels.h"

namespace mister::proto {

class AxiPadEncoder final : public ILinkPadEncoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    AxiPadEncoder(
        hal::reg::Block pad,
        const PadBlockDecl& block,
        ILinkPadEncoder& otherwise
    ) noexcept
        : lease_(std::move(pad)),
          pad_(lease_.view<hal::PadBlock>(0).value_or(hal::BlockView<hal::PadBlock>{})),
          block_(block),
          otherwise_(otherwise) {
        if (!pad_.bound())
            fatal(
                Error{Errc::slot_range, ERR_SITE(), 0},
                "AxiPadEncoder: the PAD block is shorter than its layout"
            );
    }

    [[nodiscard]] Ex<void> digital(UioOp opcode, JoyMask map) override;

    [[nodiscard]] Ex<void> stick(UioOp opcode, std::uint8_t index, StickPos pos) override;
    [[nodiscard]] std::uint32_t link_words(UioOp opcode, bool follow_up) const noexcept override;

    void trust(bool ready) noexcept;

    [[nodiscard]] bool trusted() const noexcept { return trusted_; }

    [[nodiscard]] std::uint32_t held() const noexcept { return held_; }

private:
    [[nodiscard]] Ex<void> level_(PadLevels::Slot level, PadWord word);
    [[nodiscard]] Ex<void> settle_(PadLevels::Slot level);
    [[nodiscard]] Ex<void> store_(UioOp opcode, std::uint8_t index, PadWord word);

    hal::reg::Block lease_;
    hal::BlockView<hal::PadBlock> pad_;
    const PadBlockDecl& block_;
    ILinkPadEncoder& otherwise_;
    PadLevels want_{};
    PadLevels stored_{};
    std::uint32_t held_ = 0;
    bool trusted_ = false;
};

}
