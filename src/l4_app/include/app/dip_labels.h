// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "cores/mra/dip_row.h"
#include "cores/mra/switch_table.h"
#include "infra/fixed_str.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/types.h"

namespace mister::app {

struct DipLabels {
    TASTY_SEAT_EXEMPT(component);

    proto::BindGeneration gen{};

    using Text = FixedStr<cores::mra::kDipTextChars + 1, StrFit::Clip>;

    struct Row {
        Text name{};
        proto::DipChoiceCount choices{};
        std::array<Text, cores::mra::kMaxDipIds> ids{};
    };

    proto::DipRowCount count{};
    std::array<Row, cores::mra::kMaxDipRows> rows{};
};

static_assert(std::is_trivially_copyable_v<DipLabels>);

using DipLabelsCell = xthread::Telemetry<DipLabels, SeatTag::RT>;

}
