// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/types.h"
#include "cores/window_spot.h"
#include "infra/park_timeout.h"
#include "proto/types.h"

namespace mister::cores {

struct ShareRow {
    WindowSpot page{};
    proto::FileSize bytes{};
    infra::ParkTimeout poll{infra::kForever};
    std::string_view default_dir{};
};

}
