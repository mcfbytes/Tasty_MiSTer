// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "proto/types.h"

namespace mister::cores {

struct StreamAsk {
    proto::FioIndexByte index{};
    std::string_view path{};
};

}
