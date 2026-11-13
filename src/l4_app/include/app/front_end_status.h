// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/bit_field.h"
#include "proto/types.h"

namespace mister::app {

inline constexpr proto::BitField kFrontEndBgField{.start = proto::StatusBit{1}, .width = 3};

inline constexpr proto::BitField kFrontEndPalField{.start = proto::StatusBit{4}, .width = 1};

}
