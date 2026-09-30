// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "svc/filter_bank.h"
#include "svc/filter_store.h"

namespace mister::svc {

struct FilterSet {
    std::array<FilterBank, kScalerSlots> banks{};
    std::array<std::uint8_t, kScalerSlots> modes{};
};

}  // namespace mister::svc
