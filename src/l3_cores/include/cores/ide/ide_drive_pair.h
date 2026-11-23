// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/ide/ide_types.h"
#include "infra/seat.h"

namespace mister::cores::ide {

template <class T>
struct DrivePair {
    TASTY_SEAT_EXEMPT(component);
    T master{};
    T slave{};

    [[nodiscard]] constexpr T& at(IdeDriveSelect which) noexcept {
        return which == IdeDriveSelect::Slave ? slave : master;
    }

    [[nodiscard]] constexpr const T& at(IdeDriveSelect which) const noexcept {
        return which == IdeDriveSelect::Slave ? slave : master;
    }
};

}
