// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace mister::reactor {

struct NotifierSlot {
    std::size_t v = 0;
    friend constexpr bool operator==(NotifierSlot, NotifierSlot) = default;
};

}  // namespace mister::reactor
