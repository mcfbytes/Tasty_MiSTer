// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "cores/dip_row.h"
#include "cores/switch_table.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
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
    Row rows[cores::mra::kMaxDipRows] = {};
};

static_assert(std::is_trivially_copyable_v<DipLabels>);

using DipLabelsCell = xthread::Telemetry<DipLabels, SeatTag::RT>;

}
