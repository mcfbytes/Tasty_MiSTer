// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/pad_levels.h"

#include "proto/stick_pos.h"

namespace mister::proto {

Ex<void> PadLevels::replay(PadLevels::Slot level, ILinkPadEncoder& to) const {
    const std::optional<std::uint32_t> w = words_[level];
    if (!w)
        return {};
    const Reg r = reg(level);
    if (is_digital(level))
        return to.digital(r.opcode, JoyMask{*w});
    return to.stick(r.opcode, r.index, StickPos::from_packed(*w));
}

}
