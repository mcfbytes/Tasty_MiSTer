// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"
#include "svc/disc_identity.h"

namespace mister::cores {

struct StagePlan {
    proto::FileSize disc_size_bytes{};
    bool data_first_track = false;
    svc::DiscIdText game_id{};
};

}
