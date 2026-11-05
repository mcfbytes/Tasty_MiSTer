// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/park_timeout.h"

namespace mister::xthread {

template <class P>
concept ParkLike = requires(P& p, infra::ParkTimeout ms) {
    { p.arm() } noexcept;
    { p.wait(ms) } noexcept;
    { p.disarm() } noexcept;
};

}
