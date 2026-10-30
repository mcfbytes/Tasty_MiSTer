// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/path_text.h"

namespace mister::svc {

inline constexpr std::size_t kSlotPathCap = infra::kPathMax;

using SlotPathText = infra::PathText;

}
