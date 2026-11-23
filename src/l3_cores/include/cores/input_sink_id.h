// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/message_sum.h"

namespace mister::cores {

enum class InputSinkId : std::uint8_t { None, KbdController, PacedKeycodes, kCount };

inline constexpr std::array kInputSinkIds{InputSinkId::KbdController, InputSinkId::PacedKeycodes};
static_assert(
    kInputSinkIds.size() + 1 == infra::ordinal(InputSinkId::kCount),
    "every id but None is listed"
);

}
