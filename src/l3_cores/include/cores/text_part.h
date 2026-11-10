// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "cores/part_common.h"

namespace mister::cores::mra {

struct TextPart {
    PartCommon common;
    std::string text;
};

}
