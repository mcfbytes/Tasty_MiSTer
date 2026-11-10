// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"
#include "proto/bit_field.h"
#include "proto/types.h"
#include "ui/slot_labels.h"
#include "ui/types.h"

namespace mister::ui {

[[nodiscard]] proto::FieldValue cycle_value(
    proto::BitField bits,
    proto::FieldValue current,
    CycleDir dir,
    infra::OptRef<const ISlotLabels> labels
);

}
