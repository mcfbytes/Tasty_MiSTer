// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "hal/types.h"
#include "proto/types.h"

namespace mister::cores {

struct LoadAsk {
    proto::WideIoIndex index{};
    std::string_view path{};
    proto::FileSize size{};

    hal::FabricAddr load_addr{};

    bool disc = false;
};

}
