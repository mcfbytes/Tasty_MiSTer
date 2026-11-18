// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/pad_levels.h"

namespace mister::proto {

Ex<void> PadLevels::replay(PadLevels::Slot level, ILinkPadEncoder& to) const {
    const std::optional<PadWord> w = words_[level];
    if (!w)
        return {};
    const Reg r = reg(level);
    if (is_digital(level))
        return to.digital(r.opcode, w->buttons());
    return to.stick(r.opcode, r.index, w->stick());
}

}
