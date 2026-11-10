// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/axi_pad_encoder.h"

#include "hal/uio_reg_file.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

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
    if (!block_.honours(opcode))
        return otherwise_.digital(opcode, map);
    const PadLevels::Slot s = PadLevels::level_of(opcode, 0);
    if (PadLevels::is_level(s))
        return level_(s, map.v);
    return store_(opcode, 0, map.v);
}

Ex<void> AxiPadEncoder::stick(UioOp opcode, std::uint8_t index, StickPos pos) {
    TASTY_SEAT_BODY(AxiPadEncoder);
    if (!block_.honours(opcode))
        return otherwise_.stick(opcode, index, pos);
    const std::uint32_t word = pos.packed();
    const PadLevels::Slot s = PadLevels::level_of(opcode, index);
    if (PadLevels::is_level(s))
        return level_(s, word);
    return store_(opcode, index, word);
}

void AxiPadEncoder::trust(bool ready) noexcept {
    TASTY_SEAT_BODY(AxiPadEncoder);
    trusted_ = ready;
    if (!ready)
        return;

    for (std::size_t i = 0; i < PadLevels::kCount; ++i)
        (void)settle_(PadLevels::slot_at(i));
}

std::uint32_t AxiPadEncoder::link_words(UioOp opcode, bool follow_up) const noexcept {
    TASTY_SEAT_BODY(AxiPadEncoder);
    constexpr std::uint32_t kStickWords = 4;
    if (!block_.honours(opcode))
        return otherwise_.link_words(opcode, follow_up);
    return (follow_up && !block_.honours(UioOp::Astick)) ? kStickWords : 0u;
}

Ex<void> AxiPadEncoder::level_(PadLevels::Slot level, std::uint32_t word) {
    want_.put(level, word);
    if (!trusted_) {
        ++held_;
        return {};
    }
    return settle_(level);
}

Ex<void> AxiPadEncoder::settle_(PadLevels::Slot level) {
    const std::optional<std::uint32_t> want = want_.at(level);
    if (!want || want == stored_.at(level))
        return {};
    const PadLevels::Reg r = PadLevels::reg(level);
    if (auto e = store_(r.opcode, r.index, *want); !e)
        return e;
    stored_.put(level, *want);
    return {};
}

Ex<void> AxiPadEncoder::store_(UioOp opcode, std::uint8_t index, std::uint32_t word) {
    if (!trusted_)
        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), proto::word(opcode).v});

    const auto off = static_cast<std::uint32_t>(
        hal::UioRegFile::at(pad_op(opcode), pad_index(index))
    );
    const auto reg = pad_.at<hal::reg::WO>(off);
    if (!reg)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), off});
    reg->write(word);
    return {};
}

}
