// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/relay_pad_encoder.h"

#include "infra/prelude.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::link {

RelayPadEncoder::RelayPadEncoder(BuildGeneration gen, bool gated, infra::Tally seq) noexcept {
    record_.gen = gen;
    record_.gated = gated;
    record_.seq = seq;
}

Ex<void> RelayPadEncoder::digital(UioOp opcode, JoyMask map) {
    TASTY_SEAT_BODY(RelayPadEncoder);
    return level_(opcode, 0, PadWord::of(map));
}

Ex<void> RelayPadEncoder::stick(UioOp opcode, std::uint8_t index, StickPos pos) {
    TASTY_SEAT_BODY(RelayPadEncoder);
    return level_(opcode, index, PadWord::of(pos));
}

std::uint32_t RelayPadEncoder::link_words(UioOp, bool) const noexcept {
    TASTY_SEAT_BODY(RelayPadEncoder);
    return 0;
}

void RelayPadEncoder::gate(bool gated) noexcept {
    TASTY_SEAT_BODY(RelayPadEncoder);
    if (record_.gated == gated)
        return;
    record_.gated = gated;
    moved_ = true;
}

std::optional<PadRelay> RelayPadEncoder::take_moved() noexcept {
    TASTY_SEAT_BODY(RelayPadEncoder);
    if (!moved_)
        return std::nullopt;
    moved_ = false;
    record_.seq.add();
    return record_;
}

Ex<void> RelayPadEncoder::level_(UioOp opcode, std::uint8_t index, PadWord word) {
    const PadLevels::Slot s = PadLevels::level_of(opcode, index);
    if (!PadLevels::is_level(s)) {
        refused_.add();
        return std::unexpected(proto::word(opcode).refusal(Errc::fpga_not_ready, ERR_SITE()));
    }
    PadRelay::Level& level = record_.levels[s];
    level.word = word;
    level.writes.add();
    moved_ = true;
    return {};
}

}
