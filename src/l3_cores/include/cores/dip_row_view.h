// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "proto/types.h"

namespace mister::cores {

struct DipRowView {
    std::string_view name;
    proto::DipChoiceCount choices{};
    proto::DipChoice current{};
};

}
