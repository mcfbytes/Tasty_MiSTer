// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/axi_pad_encoder.h"

#include "hal/uio_reg_file.h"

namespace mister::proto {

Ex<void> AxiPadEncoder::digital(std::uint16_t opcode, JoyMask map) {
    TASTY_SEAT_BODY(AxiPadEncoder);
    if (!block_.honours(opcode))
        return otherwise_.digital(opcode, map);
    if (const auto s = PadLevels::slot(opcode, 0))
        return level_(*s, map.v);
    return store_(opcode, 0, map.v);
}

Ex<void> AxiPadEncoder::stick(
    std::uint16_t opcode,
    std::uint8_t index,
    std::uint8_t x,
    std::uint8_t y
) {
    TASTY_SEAT_BODY(AxiPadEncoder);
    if (!block_.honours(opcode))
        return otherwise_.stick(opcode, index, x, y);
    const std::uint32_t word = PadLevels::stick_word(x, y);
    if (const auto s = PadLevels::slot(opcode, index))
        return level_(*s, word);
    return store_(opcode, index, word);
}

void AxiPadEncoder::trust(bool ready) noexcept {
    TASTY_SEAT_BODY(AxiPadEncoder);
    trusted_ = ready;
    if (!ready)
        return;

    for (std::size_t i = 0; i < PadLevels::kCount; ++i)
        (void)settle_(i);
}

std::uint32_t AxiPadEncoder::link_words(std::uint16_t opcode, bool follow_up) const noexcept {
    TASTY_SEAT_BODY(AxiPadEncoder);
    constexpr std::uint16_t kAstick = 0x1A;
    constexpr std::uint32_t kStickWords = 4;
    if (!block_.honours(opcode))
        return otherwise_.link_words(opcode, follow_up);
    return (follow_up && !block_.honours(kAstick)) ? kStickWords : 0u;
}

Ex<void> AxiPadEncoder::level_(std::size_t slot, std::uint32_t word) {
    want_.put(slot, word);
    if (!trusted_) {
        ++held_;
        return {};
    }
    return settle_(slot);
}

Ex<void> AxiPadEncoder::settle_(std::size_t slot) {
    const std::optional<std::uint32_t> want = want_.at(slot);
    if (!want || want == stored_.at(slot))
        return {};
    const PadLevels::Reg r = PadLevels::kRegs[slot];
    if (auto e = store_(r.opcode, r.index, *want); !e)
        return e;
    stored_.put(slot, *want);
    return {};
}

Ex<void> AxiPadEncoder::store_(std::uint16_t opcode, std::uint8_t index, std::uint32_t word) {
    if (!trusted_)
        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), opcode});
    const auto off = static_cast<std::uint32_t>(
        hal::UioRegFile::at(static_cast<std::uint8_t>(opcode), index)
    );
    const auto reg = pad_.at<hal::reg::WO>(off);
    if (!reg)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), off});
    reg->write(word);
    return {};
}

}
