// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/pad_block.h"
#include "hal/reg_port.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/link_pad_encoder.h"
#include "proto/link/pad_levels.h"
#include "proto/pad_word.h"

namespace mister::proto::link {

class AxiPadEncoder final : public ILinkPadEncoder {
    TASTY_SEAT_RESIDENT(Input);

public:
    using Port = hal::IRegPort<hal::PadBlock>;

    explicit AxiPadEncoder(Port& pad) noexcept : pad_(pad) {}

    AxiPadEncoder(const AxiPadEncoder&) = delete;
    AxiPadEncoder& operator=(const AxiPadEncoder&) = delete;

    [[nodiscard]] Ex<void> digital(UioOp opcode, JoyMask map) override;
    [[nodiscard]] Ex<void> stick(UioOp opcode, std::uint8_t index, StickPos pos) override;

    [[nodiscard]] std::uint32_t link_words(UioOp opcode, bool follow_up) const noexcept override;

    void put(PadLevels::Slot level, PadWord word) noexcept { want_.put(level, word); }

    [[nodiscard]] std::optional<PadWord> wanted(PadLevels::Slot level) const noexcept {
        return want_.at(level);
    }

    [[nodiscard]] bool owes() const noexcept;

    void settle() noexcept;

    void restore() noexcept;

    void open(bool leased) noexcept { open_ = leased; }

    [[nodiscard]] infra::Tally stores() const noexcept { return stores_; }

    [[nodiscard]] infra::Tally refused() const noexcept { return refused_; }

private:
    [[nodiscard]] Ex<void> fact_(UioOp opcode, std::uint8_t index, PadWord word);
    void write_(PadLevels::Reg reg, PadWord word) noexcept;

    Port& pad_;
    PadLevels want_{};
    PadLevels stored_{};
    infra::Tally stores_{};
    infra::Tally refused_{};
    bool open_ = false;
};

}
