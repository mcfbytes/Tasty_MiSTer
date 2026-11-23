// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "proto/types.h"

namespace mister::cores {

struct SidecarAsk {
    std::string_view pick{};
    proto::WideIoIndex index{};
};

}
