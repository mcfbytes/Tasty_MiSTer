// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/pad_levels.h"

namespace mister::proto {

Ex<void> PadLevels::replay(PadLevels::Slot level, ILinkPadEncoder& to) const {
    const std::optional<std::uint32_t> w = words_[level];
    if (!w)
        return {};
    const Reg r = reg(level);
    if (is_digital(level))
        return to.digital(r.opcode, JoyMask{*w});
    return to.stick(
        r.opcode,
        r.index,
        StickPos{
            static_cast<std::uint8_t>(*w & 0xFFu),
            static_cast<std::uint8_t>((*w >> 8) & 0xFFu)
        }
    );
}

}
