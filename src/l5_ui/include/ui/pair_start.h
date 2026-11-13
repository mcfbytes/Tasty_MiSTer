// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::ui {

enum class PairStart : std::uint8_t {
    Pick,
    AutoFirst,
};

static_assert(std::is_trivially_copyable_v<PairStart>);

}
