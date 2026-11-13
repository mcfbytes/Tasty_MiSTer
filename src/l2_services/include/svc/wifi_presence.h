// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc {

enum class WifiPresence : std::uint8_t {
    NoAdapter,
    NoSupplicant,
    Ready,
};

static_assert(std::is_trivially_copyable_v<WifiPresence>);
static_assert(std::has_unique_object_representations_v<WifiPresence>);

}
