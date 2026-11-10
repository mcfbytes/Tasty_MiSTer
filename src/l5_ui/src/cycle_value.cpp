// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/cycle_value.h"

namespace mister::ui {

proto::FieldValue cycle_value(
    proto::BitField bits,
    proto::FieldValue current,
    CycleDir dir,
    infra::OptRef<const ISlotLabels> labels
) {
    if (!bits.valid())
        return current;
    const proto::FieldCount slots = bits.slots();

    if (dir == CycleDir::Next) {
        const proto::FieldValue v = current.cycled_forward(slots);
        return labels && !labels->has_label(v) ? proto::FieldValue{} : v;
    }

    if (!current.within(slots))
        current = proto::FieldValue{};
    proto::FieldValue v = current.cycled_back(slots);
    while (v != current) {
        if (!labels || labels->has_label(v))
            return v;
        v = v.cycled_back(slots);
    }
    return v;
}

}
