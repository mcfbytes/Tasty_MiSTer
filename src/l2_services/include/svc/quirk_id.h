// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

enum class QuirkId : std::uint16_t {
    None = 0,
    Mssp,
    AtariVcs,
    Paddle,
    Spinner,
    LightGun,
    OpenFire,
    Wheel,

    Ds3,
    Ds4,
    Ds4Touch,
};

constexpr bool is_dualshock_like(QuirkId q) {
    return q == QuirkId::Ds3 || q == QuirkId::Ds4 || q == QuirkId::Ds4Touch;
}

constexpr bool is_pdsp_like(QuirkId q) {
    return q == QuirkId::Mssp || q == QuirkId::Paddle || q == QuirkId::Spinner;
}

}
