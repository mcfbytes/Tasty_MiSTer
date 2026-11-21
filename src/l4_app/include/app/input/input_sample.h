// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input/input_decode.h"
#include "app/input/input_emit.h"
#include "app/input/input_gates_cell.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app::input {

struct InputSample {
    TASTY_SEAT_EXEMPT(boot);
    infra::OptRef<const InputDecode::CountsCell> decode;
    infra::OptRef<const InputEmit::CountsCell> emit;
    infra::OptRef<const InputGatesCell> gates;
};

}
