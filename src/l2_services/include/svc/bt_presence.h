// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc {

enum class BtPresence : std::uint8_t {
    NoBus,
    NoDaemon,
    NoAdapter,
    Off,
    Ready,
};

static_assert(std::is_trivially_copyable_v<BtPresence>);
static_assert(std::has_unique_object_representations_v<BtPresence>);

}
