// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/axi_pad_encoder.h"

#include <cstddef>

#include "hal/uio_reg_file.h"
#include "infra/prelude.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::link {

namespace {

[[nodiscard]] constexpr hal::UioRegFile::UioOp pad_op(UioOp op) noexcept {
    return static_cast<hal::UioRegFile::UioOp>(byte(op));
}

[[nodiscard]] constexpr hal::UioRegFile::UioIndex pad_index(std::uint8_t index) noexcept {
    return static_cast<hal::UioRegFile::UioIndex>(index);
}

}

Ex<void> AxiPadEncoder::digital(UioOp opcode, JoyMask map) {
    TASTY_SEAT_BODY(AxiPadEncoder);
    return fact_(opcode, 0, PadWord::of(map));
}

Ex<void> AxiPadEncoder::stick(UioOp opcode, std::uint8_t index, StickPos pos) {
    TASTY_SEAT_BODY(AxiPadEncoder);
    return fact_(opcode, index, PadWord::of(pos));
}

std::uint32_t AxiPadEncoder::link_words(UioOp, bool) const noexcept {
    TASTY_SEAT_BODY(AxiPadEncoder);
    return 0;
}

bool AxiPadEncoder::owes() const noexcept {
    TASTY_SEAT_BODY(AxiPadEncoder);
    for (std::size_t i = 0; i < PadLevels::kCount; ++i) {
        const PadLevels::Slot level = PadLevels::slot_at(i);
        const std::optional<PadWord> want = want_.at(level);
        if (want && want != stored_.at(level))
            return true;
    }
    return false;
}

void AxiPadEncoder::settle() noexcept {
    TASTY_SEAT_BODY(AxiPadEncoder);
    for (std::size_t i = 0; i < PadLevels::kCount; ++i) {
        const PadLevels::Slot level = PadLevels::slot_at(i);
        const std::optional<PadWord> want = want_.at(level);
        if (!want || want == stored_.at(level))
            continue;
        write_(PadLevels::reg(level), *want);
        stored_.put(level, *want);
    }
}

void AxiPadEncoder::restore() noexcept {
    TASTY_SEAT_BODY(AxiPadEncoder);

    for (std::size_t i = 0; i < PadLevels::kCount; ++i) {
        const PadLevels::Slot level = PadLevels::slot_at(i);

        const PadWord word = want_.at(level).value_or(PadWord{});
        write_(PadLevels::reg(level), word);
        stored_.put(level, word);
    }
}

Ex<void> AxiPadEncoder::fact_(UioOp opcode, std::uint8_t index, PadWord word) {
    const PadLevels::Slot s = PadLevels::level_of(opcode, index);
    if (PadLevels::is_level(s)) {
        want_.put(s, word);
        return {};
    }

    if (!open_) {
        refused_.add();
        return std::unexpected(proto::word(opcode).refusal(Errc::fpga_not_ready, ERR_SITE()));
    }
    write_(PadLevels::Reg{opcode, index}, word);
    return {};
}

void AxiPadEncoder::write_(PadLevels::Reg reg, PadWord word) noexcept {

    const hal::UioRegFile::Reg at = hal::UioRegFile::at(pad_op(reg.opcode), pad_index(reg.index));
    word.write_to(pad_, at);
    stores_.add();
}

}
