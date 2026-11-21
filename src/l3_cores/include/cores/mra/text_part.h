// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "cores/mra/part_common.h"

namespace mister::cores::mra {

struct TextPart {
    PartCommon common;
    std::string text;
};

}
