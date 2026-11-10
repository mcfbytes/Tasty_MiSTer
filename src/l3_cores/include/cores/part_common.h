// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/mra_map_word.h"
#include "cores/types.h"

namespace mister::cores::mra {

struct PartCommon {
    RepeatCount repeat{1};
    MapWord map{};
};

}
