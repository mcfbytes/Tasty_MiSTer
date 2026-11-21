// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/index_set.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

using OpBitmap = infra::IndexSet<UioOp, 256, std::uint32_t>;

static_assert(
    hal::SpiWord{proto::byte(UioOp::DmaSdio)} == proto::word(UioOp::DmaSdio),
    "the last enumerator fits the bitmap's byte"
);
static_assert(std::is_trivially_copyable_v<OpBitmap> && sizeof(OpBitmap) == 32);

}
