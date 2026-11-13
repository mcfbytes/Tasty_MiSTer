// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc {

enum class BtPageMode : std::uint8_t {
    Closed,
    Paired,
    Pairing,
    PairingAll,
};

static_assert(std::is_trivially_copyable_v<BtPageMode>);
static_assert(std::has_unique_object_representations_v<BtPageMode>);

}
