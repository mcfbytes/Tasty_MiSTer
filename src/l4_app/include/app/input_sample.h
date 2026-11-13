// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input_decode.h"
#include "app/input_emit.h"
#include "app/input_gates_cell.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app {

struct InputSample {
    TASTY_SEAT_EXEMPT(boot);
    infra::OptRef<const InputDecode::CountsCell> decode;
    infra::OptRef<const InputEmit::CountsCell> emit;
    infra::OptRef<const InputGatesCell> gates;
};

}
