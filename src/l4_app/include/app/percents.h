// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/percent.h"

namespace mister::app {

struct Percents {
    Percent open{};
    Percent under{};

    friend constexpr bool operator==(Percents, Percents) = default;
};

}
