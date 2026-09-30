// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::xthread {

template <class P>
concept ParkLike = requires(P& p, int ms) {
    { p.arm() } noexcept;
    { p.wait(ms) } noexcept;
    { p.disarm() } noexcept;
};

}  // namespace mister::xthread
