// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/word_reverse.h"
#include "infra/seat_owner_io.h"

namespace mister::cores::n64 {

using SaveWordReverse = WordReverseT<seat_owner::Io>;

}
