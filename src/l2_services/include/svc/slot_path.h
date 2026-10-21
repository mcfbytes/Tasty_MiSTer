// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/fixed_str.h"

namespace mister::svc {

inline constexpr std::size_t kSlotPathCap = 1024;

using SlotPathText = FixedStr<kSlotPathCap, StrFit::Reject>;

}
