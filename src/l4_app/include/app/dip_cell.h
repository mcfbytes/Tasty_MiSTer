// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "app/dip_choices.h"
#include "app/dip_labels.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

class DipTable {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    DipLabels labels{};
    DipChoices choices{};

    [[nodiscard]] constexpr const DipLabels::Row& row(proto::DipRowIndex at) const noexcept {
        return infra::at_index(labels.rows, at);
    }

    [[nodiscard]] constexpr proto::DipChoice current(proto::DipRowIndex at) const noexcept {
        return infra::at_index(choices.current, at);
    }
};

static_assert(std::is_trivially_copyable_v<DipTable>);

}
