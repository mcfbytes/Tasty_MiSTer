// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"

namespace mister::app {

class InputDecode;
class InputBuild;
class InputEmit;
class InputWire;

struct InputSample {
    TASTY_SEAT_EXEMPT(boot);
    const InputDecode* decode = nullptr;
    const InputBuild* build = nullptr;
    const InputEmit* emit = nullptr;
    const InputWire* wire = nullptr;
};

}  // namespace mister::app
