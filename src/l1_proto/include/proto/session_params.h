// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "proto/aux_value.h"

namespace mister::proto {

struct SessionParams {
    AuxValue aux{};
    std::string_view ext{};
};

}
