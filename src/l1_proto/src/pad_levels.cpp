// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/pad_levels.h"

namespace mister::proto {

Ex<void> PadLevels::replay(std::size_t slot, ILinkPadEncoder& to) const {
    const std::optional<std::uint32_t> w = words_[slot];
    if (!w)
        return {};
    const Reg r = kRegs[slot];
    if (slot < kPorts)
        return to.digital(r.opcode, JoyMask{*w});
    return to.stick(
        r.opcode,
        r.index,
        static_cast<std::uint8_t>(*w & 0xFFu),
        static_cast<std::uint8_t>((*w >> 8) & 0xFFu)
    );
}

}
