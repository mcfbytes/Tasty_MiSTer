// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "proto/types.h"

namespace mister::svc {

struct MirrorPiece {
    proto::FileOffset at;
    std::span<const std::byte> bytes;
};

}
