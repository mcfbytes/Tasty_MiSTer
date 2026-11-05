// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

struct StreamLoadEnd {
    proto::IoIndex index{};
    proto::FileSize bytes{};
    svc::Crc32 crc{};
    bool ok = false;
};

}
