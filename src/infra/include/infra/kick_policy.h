// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>

namespace mister::xthread {

struct EventFdKick {
    static constexpr bool kModel = false;
};

struct ModelKick {
    static constexpr bool kModel = true;
};
template <class K>
concept KickPolicy = std::same_as<K, EventFdKick> || std::same_as<K, ModelKick>;

}
