// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/fixed_str.h"

namespace mister::app {

inline constexpr std::size_t kPathMax = 1024;
using PathText = FixedStr<kPathMax, StrFit::Reject>;

}  // namespace mister::app
