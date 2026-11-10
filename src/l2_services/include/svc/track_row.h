// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/pool_slot.h"
#include "svc/types.h"

namespace mister::svc {

constexpr xthread::ByteOffset TrackIndex::row_offset(xthread::SlotStride row_bytes) const noexcept {

    return row_bytes.offset_of(xthread::PoolSlot{v});
}

}
