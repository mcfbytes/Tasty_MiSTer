// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "proto/types.h"
#include "ui/types.h"

namespace mister::ui {

[[nodiscard]] constexpr RowKey slot_row_key(proto::FileRowId id) noexcept {
    constexpr std::uint16_t kMountBit = 0x100u;
    std::uint16_t bits{id.index.v};
    if (id.kind == proto::FileRowKind::Mount)
        bits |= kMountBit;
    return RowKey::in_space(RowKeySpace::Slot, RowKey::Ordinal{bits});
}

static_assert(
    std::is_same_v<decltype(proto::IoIndex::v), std::uint8_t>,
    "a wider ioctl index would collide with the slot kind bit"
);

static_assert(
    slot_row_key(proto::FileRowId::file(proto::IoIndex{0}))
    != slot_row_key(proto::FileRowId::mount(proto::IoIndex{0}))
);
static_assert(
    slot_row_key(proto::FileRowId::file(proto::IoIndex{1}))
    != slot_row_key(proto::FileRowId::mount(proto::IoIndex{1}))
);
static_assert(
    slot_row_key(proto::FileRowId::file(proto::IoIndex{0xFF}))
    != slot_row_key(proto::FileRowId::mount(proto::IoIndex{0xFF}))
);
static_assert(
    slot_row_key(proto::FileRowId::file(proto::IoIndex{0})) >= row_key_bounds(RowKeySpace::Slot).low
);
static_assert(
    slot_row_key(proto::FileRowId::mount(proto::IoIndex{0xFF}))
    <= row_key_bounds(RowKeySpace::Slot).high
);

}
